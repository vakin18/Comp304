#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h> //termios, TCSANOW, ECHO, ICANON
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include <time.h>
#include <fcntl.h>
const char *sysname = "shellfyre";
char *directory_history = "/home/vedat/dirhist.txt";
char *records = "/home/vedat/hotandcoldrecord.txt";

enum return_codes
{
	SUCCESS = 0,
	EXIT = 1,
	UNKNOWN = 2,
};

struct command_t
{
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	char *redirects[3];		// in/out redirection
	struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command)
{
	int i = 0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background ? "yes" : "no");
	printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
	printf("\tRedirects:\n");
	for (i = 0; i < 3; i++)
		printf("\t\t%d: %s\n", i, command->redirects[i] ? command->redirects[i] : "N/A");
	printf("\tArguments (%d):\n", command->arg_count);
	for (i = 0; i < command->arg_count; ++i)
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	if (command->next)
	{
		printf("\tPiped to:\n");
		print_command(command->next);
	}
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command)
{
	if (command->arg_count)
	{
		for (int i = 0; i < command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}
	for (int i = 0; i < 3; ++i)
		if (command->redirects[i])
			free(command->redirects[i]);
	if (command->next)
	{
		free_command(command->next);
		command->next = NULL;
	}
	free(command->name);
	free(command);
	return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt()
{
	char cwd[1024], hostname[1024];
	gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));
	printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command)
{
	const char *splitters = " \t"; // split at whitespace
	int index, len;
	len = strlen(buf);
	while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
	{
		buf++;
		len--;
	}
	while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
		buf[--len] = 0; // trim right whitespace

	if (len > 0 && buf[len - 1] == '?') // auto-complete
		command->auto_complete = true;
	if (len > 0 && buf[len - 1] == '&') // background
		command->background = true;

	char *pch = strtok(buf, splitters);
	command->name = (char *)malloc(strlen(pch) + 1);
	if (pch == NULL)
		command->name[0] = 0;
	else
		strcpy(command->name, pch);

	command->args = (char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index = 0;
	char temp_buf[1024], *arg;

	while (1)
	{
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch)
			break;
		arg = temp_buf;
		strcpy(arg, pch);
		len = strlen(arg);

		if (len == 0)
			continue;										 // empty arg, go for next
		while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
		{
			arg++;
			len--;
		}
		while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
			arg[--len] = 0; // trim right whitespace
		if (len == 0)
			continue; // empty arg, go for next

		// piping to another command
		if (strcmp(arg, "|") == 0)
		{
			struct command_t *c = malloc(sizeof(struct command_t));
			int l = strlen(pch);
			pch[l] = splitters[0]; // restore strtok termination
			index = 1;
			while (pch[index] == ' ' || pch[index] == '\t')
				index++; // skip whitespaces

			parse_command(pch + index, c);
			pch[l] = 0; // put back strtok termination
			command->next = c;
			continue;
		}

		// background process
		if (strcmp(arg, "&") == 0)
			continue; // handled before

		// handle input redirection
		redirect_index = -1;
		if (arg[0] == '<')
			redirect_index = 0;
		if (arg[0] == '>')
		{
			if (len > 1 && arg[1] == '>')
			{
				redirect_index = 2;
				arg++;
				len--;
			}
			else
				redirect_index = 1;
		}
		if (redirect_index != -1)
		{
			command->redirects[redirect_index] = malloc(len);
			strcpy(command->redirects[redirect_index], arg + 1);
			continue;
		}

		// normal arguments
		if (len > 2 && ((arg[0] == '"' && arg[len - 1] == '"') || (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
		{
			arg[--len] = 0;
			arg++;
		}
		command->args = (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
		command->args[arg_index] = (char *)malloc(len + 1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count = arg_index;
	return 0;
}

void prompt_backspace()
{
	putchar(8);	  // go back 1
	putchar(' '); // write empty over
	putchar(8);	  // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command)
{
	int index = 0;
	char c;
	char buf[4096];
	static char oldbuf[4096];

	// tcgetattr gets the parameters of the current terminal
	// STDIN_FILENO will tell tcgetattr that it should write the settings
	// of stdin to oldt
	static struct termios backup_termios, new_termios;
	tcgetattr(STDIN_FILENO, &backup_termios);
	new_termios = backup_termios;
	// ICANON normally takes care that one line at a time will be processed
	// that means it will return if it sees a "\n" or an EOF or an EOL
	new_termios.c_lflag &= ~(ICANON | ECHO); // Also disable automatic echo. We manually echo each char.
	// Those new settings will be set to STDIN
	// TCSANOW tells tcsetattr to change attributes immediately.
	tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

	// FIXME: backspace is applied before printing chars
	show_prompt();
	int multicode_state = 0;
	buf[0] = 0;

	while (1)
	{
		c = getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		if (c == 9) // handle tab
		{
			buf[index++] = '?'; // autocomplete
			break;
		}

		if (c == 127) // handle backspace
		{
			if (index > 0)
			{
				prompt_backspace();
				index--;
			}
			continue;
		}
		if (c == 27 && multicode_state == 0) // handle multi-code keys
		{
			multicode_state = 1;
			continue;
		}
		if (c == 91 && multicode_state == 1)
		{
			multicode_state = 2;
			continue;
		}
		if (c == 65 && multicode_state == 2) // up arrow
		{
			int i;
			while (index > 0)
			{
				prompt_backspace();
				index--;
			}
			for (i = 0; oldbuf[i]; ++i)
			{
				putchar(oldbuf[i]);
				buf[i] = oldbuf[i];
			}
			index = i;
			continue;
		}
		else
			multicode_state = 0;

		putchar(c); // echo the character
		buf[index++] = c;
		if (index >= sizeof(buf) - 1)
			break;
		if (c == '\n') // enter key
			break;
		if (c == 4) // Ctrl+D
			return EXIT;
	}
	if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
		index--;
	buf[index++] = 0; // null terminate string

	strcpy(oldbuf, buf);

	parse_command(buf, command);

	// print_command(command); // DEBUG: uncomment for debugging

	// restore the old settings
	tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
	return SUCCESS;
}

int process_command(struct command_t *command);

// Helper methods
void save_directory();
void update_records(int record);
int get_record();

int main()
{
	while (1)
	{
		struct command_t *command = malloc(sizeof(struct command_t));
		memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

		int code;
		code = prompt(command);
		if (code == EXIT)
			break;

		code = process_command(command);
		if (code == EXIT)
			break;

		free_command(command);
	}

	printf("\n");
	return 0;
}

void save_directory()
{
	/**
	 * Writes current working directory to directory history
	 */
	pid_t pid = fork();
	if (pid == 0) // child
	{
		int fd = open(directory_history, O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);

		dup2(fd, 1); // make stdout go to file
		close(fd);

		char *pwdargs[2];
		pwdargs[0] = "pwd";
		pwdargs[1] = NULL;

		execv("/bin/pwd", pwdargs);
		exit(0);
	}
	else // parent
		wait(NULL);
}

void update_records(int record)
{
	/**
	 * Updates records file with new record
	 */
	pid_t pid = fork();
	if (pid == 0) // child
	{
		/**
		 * Reset the records file
		 */
		char *resetArgs[5];
		resetArgs[0] = "truncate";
		resetArgs[1] = "-s";
		resetArgs[2] = "0";
		resetArgs[3] = records;
		resetArgs[4] = NULL;
		execv("/bin/truncate", resetArgs);
	}
	else
	{
		/**
		 * Write record to records file
		 */
		wait(NULL);
		FILE *file = fopen(records, "w");
		if (file == NULL)
			printf("Could not open records file.");
		else
			fprintf(file, "%d", record);
		fclose(file);
	}
}

int get_record()
{
	/**
	 * Return current hotandcold record
	 */
	int record;
	FILE *file = fopen(records, "r");
	fscanf(file, "%d", &record);
	fclose(file);
	return record;
}


int process_command(struct command_t *command)
{
	int r;
	if (strcmp(command->name, "") == 0)
		return SUCCESS;

	if (strcmp(command->name, "exit") == 0)
		return EXIT;

	if (strcmp(command->name, "cd") == 0)
	{
		if (command->arg_count > 0)
		{
			r = chdir(command->args[0]);
			if (r == -1)
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
			// This part is for the cdh implementation
			else
				save_directory();

			return SUCCESS;
		}
	}



	// TODO: Implement your custom commands here
	if (strcmp(command->name, "filesearch") == 0)
	{
		if (command->arg_count > 0)
		{
			pid_t pid = fork();

			if (pid == 0) // child
			{
				int recursiveFlag = 0;
				int openFileFlag = 0;
				for (int i = 0; i < command->arg_count - 1; i++) // Update flags
				{
					if (strcmp(command->args[i], "-r") == 0)
						recursiveFlag = 1;
					if (strcmp(command->args[i], "-o") == 0)
						openFileFlag = 1;
				}

				char fileName[100] = "*";
				strcat(fileName, command->args[command->arg_count - 1]);
				strcat(fileName, "*");

				char *args[command->arg_count + 2];
				int counter = 0;
				args[counter++] = "find";
				if (!recursiveFlag) // Do not check subfolders
				{
					args[counter++] = "-maxdepth";
					args[counter++] = "1";
				}
				args[counter++] = "-name";
				args[counter++] = fileName;
				args[counter++] = NULL;

				if (!openFileFlag)
					execv("/bin/find", args);

				if (openFileFlag)
				{
					int link[2];
					char foo[1024];

					if (pipe(link) == -1)
						printf("Pipe failed\n");

					pid = fork();

					if (pid == 0) // child
					{
						dup2(link[1], STDOUT_FILENO);
						close(link[0]);
						close(link[1]);
						execv("/bin/find", args);
					}

					else // parent
					{
						close(link[1]);
						int nbytes = read(link[0], foo, sizeof(foo));
						// printf("Output: (%.*s)\n", nbytes, foo);
						// printf("Bytes: %d\n", nbytes);

						if (nbytes == 0)
						{
							printf("Could not find a file.\n");
							exit(0);
						}

						char fileNames[nbytes + 10];
						strncpy(fileNames, foo, nbytes);

						char *argsForOpen[15];
						argsForOpen[0] = "cat";
						int fileCounter = 1;

						char *fileName = strtok(fileNames, "\n");
						argsForOpen[fileCounter++] = fileName;
						while (fileName != NULL)
						{
							fileName = strtok(NULL, "\n");
							argsForOpen[fileCounter++] = fileName;
						}

						execv("/bin/cat", argsForOpen);
						return 0;
					}
					return SUCCESS;
				}
			}
			else
			{
				// Wait for child to finish if command is not running in background
				if (command->args[command->arg_count - 1] != "&")
					wait(NULL);
				return SUCCESS;
			}
		}
		wait(NULL);
		return SUCCESS;
	}

	if (strcmp(command->name, "cdh") == 0)
	{
		int link[2];
		char foo[1024];

		if (pipe(link) == -1)
			printf("Pipe failed\n");

		pid_t pid = fork();

		if (pid == 0) // A.child
		{
			/**
			 * Writes the directory history into pipe
			 */
			dup2(link[1], STDOUT_FILENO);
			close(link[0]);
			close(link[1]);
			char *tacargs[3];
			tacargs[0] = "tac";
			tacargs[1] = directory_history;
			tacargs[2] = NULL;
			execv("/bin/tac", tacargs);
		}
		else // A.parent
		{
			/**
			 * Reads the directory history from pipe
			 */
			wait(NULL);
			close(link[1]);
			int nbytes = read(link[0], foo, sizeof(foo));
			int dirCounter = 0;
			char *directories[10];
			char *dirName = strtok(foo, "\n");

			directories[dirCounter++] = dirName;
			while (dirName != NULL && dirCounter < 10)
			{
				dirName = strtok(NULL, "\n");
				directories[dirCounter++] = dirName;
			}

			/**
			 * Prints directory history to terminal
			 */
			for (int i = 0; i < dirCounter - 1; i++)
			{
				printf("%c %d) %s\n", 'a' + i, i + 1, directories[i]);
			}

			int inputLimit = 2;
			int fd[2];
			char input[inputLimit];

			if (pipe(fd) == -1)
				printf("Pipe failed\n");

			pid = fork();
			if (pid == 0) // Child
			{
				/**
				 * Gets user input to change directory
				 * Writes it to pipe
				 */
				printf("Select directory by letter or number: ");
				fgets(input, inputLimit, stdin); // Get string input
				write(fd[1], input, sizeof(inputLimit));
				close(fd[0]);
				exit(0);
			}

			else // Parent
			{
				/**
				 * Reads user input from pipe
				 * Changes directory
				 * Updates directory history
				 */
				wait(NULL);
				read(fd[0], input, sizeof(inputLimit));

				for (int i = 0; i < dirCounter - 1; i++)
				{
					if (input[0] == 'a' + i || atoi(input) == i + 1)
					{
						r = chdir(directories[i]);
						save_directory();
						return SUCCESS;
					}
				}
			}
		}
	}

	if (strcmp(command->name, "take") == 0)
	{
		if (command->arg_count == 1)
		{
			pid_t pid = fork();

			if (pid == 0) // child
			{
				/**
				 * Creates directory if does not exist
				 */
				char *mkdirArgs[4];
				mkdirArgs[0] = "mkdir";
				mkdirArgs[1] = "-p";
				mkdirArgs[2] = command->args[0];
				mkdirArgs[3] = NULL;
				execv("/bin/mkdir", mkdirArgs);
			}

			else
			{
				/**
				 * Changes directory
				 * Updates directory history
				 */
				wait(NULL);
				char *cdArgs[2];
				cdArgs[0] = "cd";
				cdArgs[1] = command->args[0];
				r = chdir(command->args[0]);
				if (r == -1)
					printf("-%s: %s: %s\n", sysname, "cd", strerror(errno));
				else
					save_directory();
			}
		}
	}
	
	if (strcmp(command->name, "joker") == 0)
	{
		/**
		 * Opens temporary file to schedule crontab task
		 */
		FILE *file = fopen("cronFile", "w");
		fprintf(file, "*/15 * * * *  XDG_RUNTIME_DIR=/run/user/$(id -u) notify-send \"$(curl https://icanhazdadjoke.com)\"\n");
		fclose(file);

		pid_t pid = fork();
		if (pid == 0) // child
		{
			/**
			 * Schedules crontab task
			 */
			char *cronArgs[3];
			cronArgs[0] = "crontab";
			cronArgs[1] = "cronFile";
			cronArgs[2] = NULL;
			execv("/bin/crontab", cronArgs);
			exit(0);
		}
		else
		{
			wait(NULL);
			pid = fork();
			if (pid == 0) // child
			{
				/**
				 * Removes temporary file
				 */
				char *removeArgs[3];
				removeArgs[0] = "rm";
				removeArgs[1] = "cronFile";
				removeArgs[2] = NULL;
				execv("/bin/rm", removeArgs);
				exit(0);
			}
			else // parent
			{
				wait(NULL);
				return SUCCESS;
			}
		}
	}

	if (strcmp(command->name, "joke") == 0)
	{
		/**
		 * Prints one joke
		 */
		pid_t pid = fork();

		if (pid == 0) // child
		{
			char *curlArgs[3];
			curlArgs[0] = "curl";
			curlArgs[1] = "https://icanhazdadjoke.com";
			curlArgs[2] = NULL;

			execv("/bin/curl", curlArgs);
		}
		else
		{
			wait(NULL);
			printf("\n");
		}
		return SUCCESS;
	}
	
	if (strcmp(command->name, "hotandcold") == 0)
	{
		/**
		 * Takes guesses from user
		 * If got closer, prints hotter
		 * If got further, prints closer
		 */
		pid_t pid = fork();

		if (pid == 0) // child
		{
			int guess, distance, counter, record;
			counter = 0;
			srand(time(NULL));		  // Init rand
			int r = rand() % 100 + 1; // from 1 to 100
			printf("Make a guess from 1 to 100: ");
			scanf("%d", &guess);
			counter++;
			distance = 100;

			while (guess != r)
			{
				// got closer
				if (abs(guess - r) <= distance)
					printf("Getting hot!\n");
				else // got further
					printf("Getting cold!\n");

				distance = abs(guess - r);

				printf("Make a guess: ");
				scanf("%d", &guess);
				counter++;
			}
			printf("You guessed in %d tries.\n", counter);

			// Check if it is a new record
			record = get_record();

			// New Record
			if ((counter < record) || (record == -1))
			{
				printf("Congratulations! That's a new record!\n");
				update_records(counter);
			}
			exit(0);
		}
		else
		{
			wait(NULL);
			return SUCCESS;
		}
	}

	if (strcmp(command->name, "resetrecord") == 0)
	{
		update_records(-1);
		printf("The record is reset.\n");
		return SUCCESS;
	}

	if (strcmp(command->name, "pstraverse") == 0)
	{
		if (command->arg_count != 2) // Require two parameters
		{
			printf("Invalid input.\n");
			return SUCCESS;
		}

		if (strcmp(command->args[1], "-b") != 0 && strcmp(command->args[1], "-d") != 0) // If not -b or -d
		{
			printf("Invalid input.\n");
			return SUCCESS;
		}

		pid_t pid = fork();

		if (pid == 0) // child
		{
			char *sudoDmesgArgs[4];
			sudoDmesgArgs[0] = "sudo";
			sudoDmesgArgs[1] = "dmesg";
			sudoDmesgArgs[2] = "-C";
			sudoDmesgArgs[3] = NULL;
			execv("/bin/sudo", sudoDmesgArgs);
			exit(0);
		}
		else // parent
		{
			wait(NULL);
			pid = fork();
			if (pid == 0) // child
			{
				/**
				 * Calls insmod line with parameters
				 */
				char pid[10] = "PID=";
				char *pidVal = command->args[0];
				strcat(pid, pidVal);

				char traverse[20] = "traverseType=\"";
				char *traverseVal = command->args[1];
				strcat(traverse, traverseVal);
				strcat(traverse, "\"");

				char *insmodArgs[6];
				insmodArgs[0] = "sudo";
				insmodArgs[1] = "insmod";
				insmodArgs[2] = "my_module.ko";
				insmodArgs[3] = pid;
				insmodArgs[4] = traverse;
				insmodArgs[5] = NULL;
				execv("/bin/sudo", insmodArgs);
				exit(0);
			}
			else // parent
			{
				wait(NULL);
				pid = fork();
				if (pid == 0) // child
				{
					char *rmmodArgs[4];
					rmmodArgs[0] = "sudo";
					rmmodArgs[1] = "rmmod";
					rmmodArgs[2] = "my_module.ko";
					rmmodArgs[3] = NULL;
					execv("/bin/sudo", rmmodArgs);
					exit(0);
				}
				else // parent
				{
					wait(NULL);
					pid = fork();
					if (pid == 0) // child
					{
						char *dmesg[2];
						dmesg[0] = "dmesg";
						dmesg[1] = NULL;
						execv("/bin/dmesg", dmesg);
						exit(0);
					}
					else // parent
					{
						wait(NULL);
						return SUCCESS;
					}
				}
			}
		}
	}

	
	
	// Custom commands until here

	pid_t pid = fork();

	if (pid == 0) // child
	{
		// increase args size by 2
		command->args = (char **)realloc(
			command->args, sizeof(char *) * (command->arg_count += 2));

		// shift everything forward by 1
		for (int i = command->arg_count - 2; i > 0; --i)
			command->args[i] = command->args[i - 1];

		// set args[0] as a copy of name
		command->args[0] = strdup(command->name);
		// set args[arg_count-1] (last) to NULL
		command->args[command->arg_count - 1] = NULL;

		// TODO: do your own exec with path resolving using execv()

		char path[] = "/bin/";
		strcat(path, command->name);

		execv(path, command->args);
		exit(0);
	}
	else
	{
		// TODO: Wait for child to finish if command is not running in background
		if (command->args[command->arg_count - 1] != "&")
		{
			wait(NULL);
		}
		return SUCCESS;
	}

	printf("-%s: %s: command not found\n", sysname, command->name);
	return UNKNOWN;
}
