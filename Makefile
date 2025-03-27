all:
	@cc -Wall -Wextra -std=c99 -D_POSIX_C_SOURCE=200809L \
	    gaze.c -lncurses -o gaze

style:
	@clang-format-21 -i -style=file:clang_format gaze.c

clean:
	@rm -f gaze

install: all
	@mv gaze /usr/bin/gaze

uninstall:
	@rm /usr/bin/gaze
