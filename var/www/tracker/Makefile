.PHONY: style

style:
	bash -c 'for i in $$( find . -name "*.js" ); do js-beautify -r $$i; done'
	bash -c 'for i in $$( find . -name "*.php" ); do php-cs-fixer fix $$i  --rules='{"no_closing_tag": false}'; done'
#	bash -c 'for i in $$( find . -name "*.css" ); do ~/node_modules/.bin/cssbeautify-cli -w $$i.1 -f $$i; mv $$i.1 $$i; done'
