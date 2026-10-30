.PHONY: all clean
all: main.pdf

main.pdf: main.tex results.tex refs.bib $(wildcard *.png)
	pdflatex -interaction=nonstopmode main >/dev/null
	bibtex main >/dev/null
	pdflatex -interaction=nonstopmode main >/dev/null
	pdflatex -interaction=nonstopmode main >/dev/null
	@grep -E "^!|undefined" main.log || echo "main.pdf built cleanly"

clean:
	rm -f main.aux main.bbl main.blg main.log main.out main.pdf
