all : pdf clean_aux

pdf:
		latexmk -pdf -bibtex thesis.tex

grammarcheck:
		textidote --html thesis.tex > grammar.html
		firefox grammar.html

clean_aux:
		latexmk -bibtex -c

clean : clean_aux
		rm thesis.pdf

rebuild : clean all
