init:
	@find .git/hooks -type l -exec rm {} \;
	@find .githooks -type f -exec ln -sf ../../{} .git/hooks/ \;

format:
	@clang-format -style=file *.c* *.h* -i
