#!/usr/bin/env bash
# aws_bench.sh — run the decode benchmark on a fresh EC2 server, copy the results back,
# and ALWAYS terminate the server (on success, failure, or Ctrl-C).
#
# Run from your laptop (WSL) inside ~/thesis/bench_isal, with the AWS CLI configured:
#   ./aws_bench.sh                     # auto-picks the largest c7i that fits your vCPU quota
#   TYPE=c7i.8xlarge ./aws_bench.sh    # force a size (16 physical cores, needs a 32-vCPU quota)
#   REGION=ap-southeast-1 ./aws_bench.sh
#
# c7i = Intel Sapphire Rapids: AVX2, AVX-512 and GFNI. Results land in results/<hostname>-<date>/.
# Keep the laptop awake while it runs (about 45-60 min). A safety timer on the server shuts it
# down (and therefore terminates it) after 3 hours even if this script dies.

set -euo pipefail
cd "$(dirname "$0")"
command -v aws >/dev/null || { echo "aws CLI not found"; exit 1; }

REGION=${REGION:-$(aws configure get region 2>/dev/null || true)}
REGION=${REGION:-ap-southeast-1}
export AWS_DEFAULT_REGION=$REGION
NAME=fec-bench-$(date +%Y%m%d-%H%M%S)
KEYFILE=$HOME/.ssh/$NAME.pem
SSHOPT=(-i "$KEYFILE" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR
        -o ConnectTimeout=10 -o ServerAliveInterval=30 -o ServerAliveCountMax=10)
IID=""; SG=""; KEY_CREATED=0

cleanup() {
  set +e
  echo ">> cleanup"
  if [ -n "$IID" ]; then
    aws ec2 terminate-instances --instance-ids "$IID" >/dev/null && echo "   terminating $IID"
    aws ec2 wait instance-terminated --instance-ids "$IID" && echo "   $IID terminated"
  fi
  [ -n "$SG" ] && aws ec2 delete-security-group --group-id "$SG" >/dev/null 2>&1 && echo "   security group deleted"
  if [ "$KEY_CREATED" = 1 ]; then aws ec2 delete-key-pair --key-name "$NAME" >/dev/null 2>&1; rm -f "$KEYFILE"; echo "   key pair deleted"; fi
}
trap cleanup EXIT
trap 'exit 130' INT TERM

echo ">> account: $(aws sts get-caller-identity --query Account --output text)   region: $REGION"

# ── pick an instance size that fits the On-Demand Standard vCPU quota ───────
if [ -z "${TYPE:-}" ]; then
  Q=$(aws service-quotas get-service-quota --service-code ec2 --quota-code L-1216C47A \
        --query 'Quota.Value' --output text 2>/dev/null || echo 0)
  Q=${Q%.*}
  if   [ "${Q:-0}" -ge 32 ]; then TYPE=c7i.8xlarge
  elif [ "${Q:-0}" -ge 16 ]; then TYPE=c7i.4xlarge
  elif [ "${Q:-0}" -ge 8 ];  then TYPE=c7i.2xlarge
  else
    echo "Your On-Demand Standard vCPU quota is ${Q:-unknown} in $REGION (need >= 8)."
    echo "Request an increase: Service Quotas > Amazon EC2 > 'Running On-Demand Standard instances',"
    echo "or set TYPE explicitly if you know your quota."
    exit 1
  fi
fi
echo ">> instance type: $TYPE"

AMI=$(aws ssm get-parameter --name /aws/service/canonical/ubuntu/server/24.04/stable/current/amd64/hvm/ebs-gp3/ami-id \
        --query Parameter.Value --output text)
echo ">> AMI: $AMI (Ubuntu 24.04)"

mkdir -p "$HOME/.ssh"
aws ec2 create-key-pair --key-name "$NAME" --query KeyMaterial --output text > "$KEYFILE"
chmod 600 "$KEYFILE"; KEY_CREATED=1

MYIP=$(curl -s https://checkip.amazonaws.com)
SG=$(aws ec2 create-security-group --group-name "$NAME" --description "fec-bench temporary SSH" --query GroupId --output text)
aws ec2 authorize-security-group-ingress --group-id "$SG" --protocol tcp --port 22 --cidr "$MYIP/32" >/dev/null
echo ">> SSH allowed only from $MYIP"

USERDATA=$(printf '#!/bin/bash\nshutdown -h +180\n')   # safety net: 3 h, then shutdown => terminate
IID=$(aws ec2 run-instances --image-id "$AMI" --instance-type "$TYPE" --key-name "$NAME" \
        --security-group-ids "$SG" --instance-initiated-shutdown-behavior terminate \
        --block-device-mappings 'DeviceName=/dev/sda1,Ebs={VolumeSize=30,VolumeType=gp3,DeleteOnTermination=true}' \
        --user-data "$USERDATA" \
        --tag-specifications "ResourceType=instance,Tags=[{Key=Name,Value=$NAME}]" \
        --query 'Instances[0].InstanceId' --output text)
echo ">> launched $IID"
aws ec2 wait instance-running --instance-ids "$IID"
IP=$(aws ec2 describe-instances --instance-ids "$IID" --query 'Reservations[0].Instances[0].PublicIpAddress' --output text)
echo ">> running at $IP; waiting for SSH"
for _ in $(seq 1 60); do ssh "${SSHOPT[@]}" ubuntu@"$IP" true 2>/dev/null && break; sleep 5; done

echo ">> uploading code"
tar czf - -C .. FEC_Codec_v3 \
    --exclude='bench_isal/results' --exclude='bench_isal/cm256-src' --exclude='bench_isal/isal-*' \
    --exclude='bench_isal/test_simd' --exclude='bench_isal/bench_isal' bench_isal \
  | ssh "${SSHOPT[@]}" ubuntu@"$IP" 'mkdir -p thesis && tar xzf - -C thesis'

echo ">> installing packages and starting the benchmark (log: ~/run.log on the server)"
ssh "${SSHOPT[@]}" ubuntu@"$IP" 'bash -s' <<'REMOTE'
set -e
sudo DEBIAN_FRONTEND=noninteractive apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq build-essential nasm libisal-dev git \
     python3-pandas python3-matplotlib linux-tools-common "linux-tools-$(uname -r)" >/dev/null || \
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq build-essential nasm libisal-dev git python3-pandas python3-matplotlib >/dev/null
# Best effort: EC2 guests usually do not expose frequency control; machine.txt records what was possible.
sudo cpupower frequency-set -g performance >/dev/null 2>&1 || true
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo >/dev/null 2>&1 || true
cd ~/thesis/bench_isal
nohup bash -c 'make test && ./run_all.sh; echo EXIT:$? > ~/done' > ~/run.log 2>&1 &
REMOTE

echo ">> benchmark running; polling every 60 s"
START=$(date +%s)
while :; do
  sleep 60
  if ssh "${SSHOPT[@]}" ubuntu@"$IP" 'test -f ~/done' 2>/dev/null; then break; fi
  LAST=$(ssh "${SSHOPT[@]}" ubuntu@"$IP" 'tail -1 ~/run.log' 2>/dev/null || echo "(ssh hiccup)")
  echo "   $(( ($(date +%s) - START) / 60 )) min: ${LAST:0:110}"
done
RC=$(ssh "${SSHOPT[@]}" ubuntu@"$IP" 'cat ~/done')
echo ">> finished: $RC"

mkdir -p results
ssh "${SSHOPT[@]}" ubuntu@"$IP" 'cd ~/thesis/bench_isal/results && tar czf - .' | tar xzf - -C results
scp "${SSHOPT[@]}" ubuntu@"$IP":run.log "results/aws-$NAME-run.log" >/dev/null
echo ">> results copied:"; ls -d results/*/ | tail -3
[ "$RC" = "EXIT:0" ] || { echo "benchmark failed — see results/aws-$NAME-run.log"; exit 1; }
echo ">> done (instance will now be terminated)"
