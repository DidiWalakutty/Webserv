CC = c++
CFLAGS = -std=c++23 -Iincludes/ -Wall -Wall -Werror -O3
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

fclean: clean
	rm -f $(NAME)

re: fclean $(NAME)