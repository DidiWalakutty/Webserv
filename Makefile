CC = c++
CFLAGS = -std=c++20 -Iincludes/ -Wall -Werror -Wextra -O3
LDFLAGS =

SRCDIR = sources
SOURCES = $(wildcard $(SRCDIR)/*.cpp)
OBJDIR = objects
OBJECTS = $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))
NAME = webserv

.PHONY: run clean fclean re

$(NAME): $(OBJDIR) $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $(NAME) $(LDFLAGS)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	$(CC) $(CFLAGS) -c $< -o $@

run: $(NAME)
	./$(NAME)

clean:
	rm -rf $(OBJDIR)
	rm -rf www/upload/upload_2026*
	rm -rf www/html/upload_2026*

fclean: clean
	rm -f $(NAME)

re: fclean $(NAME)