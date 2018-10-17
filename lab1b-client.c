// NAME: MATTHEW PATERNO
// EMAIL: MPATERNO@G.UCLA.EDU
// ID: 904756085

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/socket.h>
#include <poll.h>
#include <netinet/in.h>
#include <netdb.h>

struct termios defaultTermState;
struct termios newTermState;
int shellFlag;
int pipeToChild[2];
int pipeToParent[2];
char *shellProgram = NULL;
int portNumber = 0;
char *logFile = NULL;
char *host = NULL;
char *fileToEncrypt = NULL;
char crlf[2] = {'\r', '\n'};

pid_t pid;
extern int errno;
int sockfd, n;
struct sockaddr_in serv_addr;
struct hostent *server;

void defaultio()
{
  char buffer[256];
  int readSize = read(STDIN_FILENO, buffer, sizeof(char) * 256);
  while (readSize)
  {
    int i = 0;
    while (i < readSize)
    {
      char current = buffer[i];
      if (current == '\r' || current == '\n')
        write(STDOUT_FILENO, crlf, 2 * sizeof(char));
      else if (current == '\4')
        exit(0);
      else
        write(STDOUT_FILENO, &buffer[i], sizeof(char));
      i++;
    }
    readSize = read(STDIN_FILENO, buffer, sizeof(char) * 256);
  }
  if (readSize < 0)
  {
    fprintf(stderr, "ERROR: Bad non-shell read, error number: %s\n ", strerror(errno));
    exit(1);
  }
}

void initializePipes(int fd1[2], int fd2[2])
{
  if (pipe(fd1) == -1)
  {
    fprintf(stderr, "ERROR: Bad pipe creation for fd1, error number: %s\n ", strerror(errno));
    exit(1);
  }
  if (pipe(fd2) == -1)
  {
    fprintf(stderr, "ERROR: Bad pipe creation for fd2, error number: %s\n ", strerror(errno));
    exit(1);
  }
}

struct pollfd pollfds[2];
void configurePollfd()
{
  pollfds[0].fd = STDIN_FILENO;
  pollfds[0].events = POLLIN;
  pollfds[1].fd = pipeToParent[0];
  pollfds[1].events = POLLIN | POLLHUP | POLLERR;
}

void shellio() // Sending 0 and 1
{
  close(pipeToChild[0]);  // Close Read End
  close(pipeToParent[1]); // Close Write End
  configurePollfd();

  while (1)
  {
    char buffer[256];
    int pollReturn = poll(pollfds, 2, 0);

    if (pollReturn < 0)
    {
      fprintf(stderr, "ERROR: Bad poll creation, error number: %s\n ", strerror(errno));
      exit(1);
    }
    else
    {
      if (pollfds[0].revents & POLLIN)
      {
        // Pipe input from keyboard to shell.
        int readSize = read(STDIN_FILENO, buffer, sizeof(char) * 256);
        if (readSize < 0)
        {
          fprintf(stderr, "ERROR: Bad from STDIN, error number: %s\n ", strerror(errno));
          exit(1);
        }

        int i = 0;
        while (i < readSize)
        {
          char current = buffer[i];
          if (current == '\r' || current == '\n')
          {
            char n = '\n';
            if (write(STDOUT_FILENO, crlf, 2 * sizeof(char)) < 0)
              fprintf(stderr, "ERROR: Bad write to STDOUT, error number: %s\n ", strerror(errno));
            write(pipeToChild[1], &n, sizeof(char));
          }
          else if (current == '\4')
          {
            close(pipeToChild[1]);
            exit(0);
          }
          else if (current == '\3')
            kill(pid, SIGINT);
          else
          {
            write(STDOUT_FILENO, &buffer[i], sizeof(char));
            write(pipeToChild[1], &buffer[i], sizeof(char));
          }
          i++;
        }
        if (readSize < 0)
        {
          fprintf(stderr, "ERROR: Unable to Read, error number: %s\n ", strerror(errno));
          exit(1);
        }
      }
      else if (pollfds[1].revents & POLLIN)
      {
        // Read From Shell
        int readSize = read(pipeToParent[0], buffer, sizeof(char) * 256);
        int i = 0;
        if (readSize < 0)
        {
          fprintf(stderr, "ERROR: Bad read from shell, error number: %s\n ", strerror(errno));
          exit(1);
        }
        while (i < readSize)
        {
          char current = buffer[i];
          if (current == '\n' || current == '\r')
            write(STDOUT_FILENO, crlf, 2 * sizeof(char));
          else
            write(STDOUT_FILENO, &buffer[i], sizeof(char));
          i++;
        }
      }
      else if (pollfds[1].revents & (POLLHUP | POLLERR))
      {
        fprintf(stderr, "ERROR: Poll error, error number: %s\n ", strerror(errno));
        break;
      }
    }
  }
}

void shellProcess()
{
  // Close Other Read and Write Ends
  close(pipeToChild[1]);
  close(pipeToParent[0]);
  // Reassign file descriptors.
  dup2(pipeToChild[0], 0);
  dup2(pipeToParent[1], 1);
  // Close non-copies.
  close(pipeToChild[0]);
  close(pipeToParent[1]);

  char *args[2] = {shellProgram, NULL};
  if (execvp(shellProgram, args) == -1)
  { //execute shell
    fprintf(stderr, "ERROR: Bad shell execution, error number: %s\n ", strerror(errno));
    exit(1);
  }
}

// Function runs at exit.
void restoreTerminal()
{
  tcsetattr(STDIN_FILENO, TCSANOW, &defaultTermState);
  if (shellFlag)
  {
    int status;
    if (waitpid(pid, &status, 0) == -1)
    {
      fprintf(stderr, "ERROR: Waitpid, error number: %s\n ", strerror(errno));
      exit(1);
    }
    if (WIFEXITED(status))
    {
      const int exitStatus = WEXITSTATUS(status);
      const int termSig = WTERMSIG(status);
      fprintf(stderr, "SHELL EXIT SIGNAL=%d STATUS=%d\n", termSig, exitStatus);
    }
  }
}

void setTerminal()
{
  // Store Terminal Attributes to defaultTermState
  tcgetattr(STDIN_FILENO, &defaultTermState);
  // Save for restoration
  tcsetattr(STDIN_FILENO, TCSANOW, &defaultTermState); // Sets new parameters immediately.
  tcgetattr(STDIN_FILENO, &newTermState);

  // Set New Attributes
  newTermState.c_iflag = ISTRIP;
  newTermState.c_oflag = 0;
  newTermState.c_lflag = 0;

  if (tcsetattr(0, TCSANOW, &newTermState) < 0)
  {
    fprintf(stderr, "ERROR: Unable to set terminal attributes, error number: %s\n ", strerror(errno));
    exit(1);
  }
}

void createSocket()
{
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
  {
    fprintf(stderr, "ERROR: Unable to open sock, error number: %s\n ", strerror(errno));
    exit(1);
  }
  server = gethostbyname("localhost");
  bzero((char *)&serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  bcopy((char *)server->h_addr,
        (char *)&serv_addr.sin_addr.s_addr,
        server->h_length);
  serv_addr.sin_port = htons(portNumber);
  if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
  {
    fprintf(stderr, "ERROR: Unable to connect correctly, error number: %s\n ", strerror(errno));
    exit(1);
  }
}

void writeToSocket()
{
  while (1)
  {
    char buffer[256];
    int readSize = read(STDIN_FILENO, buffer, sizeof(char) * 256);
    if (readSize < 0)
    {
      fprintf(stderr, "ERROR: Bad from STDIN, error number: %s\n ", strerror(errno));
      exit(1);
    }
    printf("In Here");
    // Process Buffer
    int i = 0;
    while (i < readSize)
    {
      char current = buffer[i];
      if (current == '\r' || current == '\n')
      {
        char n = '\n';
        if (write(STDOUT_FILENO, crlf, 2 * sizeof(char)) < 0)
          fprintf(stderr, "ERROR: Bad write to STDOUT, error number: %s\n ", strerror(errno));
        if (write(sockfd, &buffer[i], sizeof(char) < 0))
          fprintf(stderr, "ERROR: Bad write to STDOUT, error number: %s\n ", strerror(errno));
      }
      else if (current == '\4')
      {
        if (write(sockfd, &buffer[i], sizeof(char) < 0))
          fprintf(stderr, "ERROR: Bad write to STDOUT, error number: %s\n ", strerror(errno));
      }
      else if (current == '\3')
      {
        if (write(sockfd, &buffer[i], sizeof(char) < 0))
          fprintf(stderr, "ERROR: Bad write to STDOUT, error number: %s\n ", strerror(errno));
      }
      else
      {
        write(STDOUT_FILENO, &buffer[i], sizeof(char));
        n = write(sockfd, &buffer[i], sizeof(char));
        if (n < 0)
          fprintf(stderr, "ERROR: Bad write to STDOUT, error number: %s\n ", strerror(errno));
      }
      i++;
    }
    if (readSize < 0)
    {
      fprintf(stderr, "ERROR: Unable to Read, error number: %s\n ", strerror(errno));
      exit(1);
    }
  }
}

int main(int argc, char **argv)
{
  setTerminal();
  atexit(restoreTerminal);
  shellFlag = 0;

  static struct option long_options[] =
      {
          {"shell", optional_argument, 0, 's'},
          {"port", required_argument, 0, 'p'},
          {"host", optional_argument, 0, 'h'},
          {"encrypt", optional_argument, 0, 'e'},
          {"log", optional_argument, 0, 'l'},
      };

  int c;

  // Set Options
  while (1)
  {
    // int option_index = 0;
    c = getopt_long(argc, argv, "s:p", long_options, NULL);
    /* Detect end of options */
    if (c == -1)
      break;
    else if (c == 's')
    {
      shellFlag = 1;
      if (optarg != NULL)
        shellProgram = optarg;
    }
    else if (c == 'p')
    {
      if (optarg != NULL)
        portNumber =
            (int)strtol(optarg, (char **)NULL, 10);
    }
    else if (c == 'l')
    {
      if (optarg != NULL)
        logFile = optarg;
    }
    else if (c == 'h')
    {
      if (optarg != NULL)
        host = optarg;
    }
    else if (c == 'e')
    {
      if (optarg != NULL)
        fileToEncrypt = optarg;
    }
    else
    {
      fprintf(stderr, "ERROR: Unrecognized arguments, error number: %s\n ", strerror(errno));
      exit(1);
    }
  }
  // initializePipes(pipeToChild, pipeToParent);

  createSocket();
  writeToSocket();

  if (shellFlag)
  {
    pid = fork(); // Create new process and store ID.
    if (pid == 0) // Child Process
      shellProcess();
    else if (pid > 0) // Parent Process
      shellio();
    else
    {
      fprintf(stderr, "error: %s", strerror(errno));
      exit(1);
    }
  }
  else
    defaultio();
  exit(0);
}
