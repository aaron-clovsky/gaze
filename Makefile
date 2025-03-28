all:
	@cc -Wall -Wextra -D_POSIX_C_SOURCE=200809L \
	    gaze.c -lncurses -o gaze

clean:
	@rm -f gaze

install: all
	@mv gaze /usr/bin/gaze

uninstall:
	@rm /usr/bin/gaze

style:
	@clang-format-21 -i -style=file:clang_format gaze.c

lint:
	@echo Testing...
	@echo " gcc in C mode:"
	@gcc -Wall -Wextra -D_POSIX_C_SOURCE=200809L gaze.c -lncurses -o gaze
	@echo " clang in C mode:"
	@clang -Wall -Wextra -D_POSIX_C_SOURCE=200809L gaze.c -lncurses -o gaze
	@echo " gcc in C++ mode:"
	@cp gaze.c gaze.cpp
	@g++ -Wall -Wextra -D_POSIX_C_SOURCE=200809L gaze.cpp \
	     -lncurses -o gaze
	@echo " clang in C++ mode:"
	@clang++ -Wall -Wextra -D_POSIX_C_SOURCE=200809L gaze.cpp \
	         -lncurses -o gaze
	@rm gaze.cpp
	@echo -n " cppcheck: "
	@cppcheck --enable=all --suppress=missingIncludeSystem \
	          --inconclusive --check-config --std=c99 gaze.c
