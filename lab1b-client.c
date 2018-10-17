// NAME: MATTHEW PATERNO
// EMAIL: MPATERNO@G.UCLA.EDU
// ID: 904756085

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
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
int logFlag = 0;
int logfd = -1;
char crlf[2] = {'\r', '\n'};

pid_t pid;
extern int errno;
int sockfd, n;
struct sockaddr_in serv_addr;
struct hostent *server;

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
  pollfds[1].fd = sockfd;
  pollfds[1].events = POLLIN | POLLHUP | POLLERR;
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

void logSent(char cur)
{
  char c = '\n';
  write(logfd, "SENT 4 bytes: ", sizeof(char) * 14);
  write(logfd, &cur, sizeof(char));
  write(logfd, &c, sizeof(char));
}

void logReceived(char cur)
{
  char c = '\n';
  write(logfd, "RECEIVED 4 bytes: ", sizeof(char) * 17);
  write(logfd, &cur, sizeof(char));
  write(logfd, &c, sizeof(char));
}

void newWriteSocket()
{
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
          if (logFlag)
          {
            logSent(current);
          }
          if (current == '\r' || current == '\n')
          {
            if (write(STDOUT_FILENO, crlf, 2 * sizeof(char)) < 0)
            {
              fprintf(stderr, "ERROR: Bad write to STDOUT, error number: %s\n ", strerror(errno));
              exit(1);
            }
            if (write(sockfd, &buffer[i], sizeof(char)) < 0)
            {
              fprintf(stderr, "ERROR: Bad write to socket, error number: %s\n ", strerror(errno));
              exit(1);
            }
          }
          else if (current == '\4')
          {
            if (write(sockfd, &buffer[i], sizeof(char)) < 0)
            {
              fprintf(stderr, "ERROR: Bad write to socket, error number: %s\n ", strerror(errno));
              exit(1);
            }
          }
          else if (current == '\3')
          {
            if (write(sockfd, &buffer[i], sizeof(char)) < 0)
            {
              fprintf(stderr, "ERROR: Bad write to socket, error number: %s\n ", strerror(errno));
              exit(1);
            }
          }
          else
          {
            if (write(STDOUT_FILENO, &buffer[i], sizeof(char)) < 0)
            {
              fprintf(stderr, "ERROR: Bad write to STDOUT, error number: %s\n ", strerror(errno));
              exit(1);
            }
            if (write(sockfd, &buffer[i], sizeof(char)) < 0)
            {
              fprintf(stderr, "ERROR: Bad write to socket, error number: %s\n ", strerror(errno));
              exit(1);
            }
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
        int readSize = read(sockfd, buffer, sizeof(char) * 256);
        int i = 0;
        if (readSize < 0)
        {
          fprintf(stderr, "ERROR: Bad read from shell, error number: %s\n ", strerror(errno));
          exit(1);
        }
        while (i < readSize)
        {
          char current = buffer[i];
          if (logFlag)
          {
            logReceived(buffer[i]);
          }
          if (current == '\n' || current == '\r')
          {
            if (write(STDOUT_FILENO, crlf, 2 * sizeof(char)) < 0)
              fprintf(stderr, "ERROR: Bad write to stdout, error number: %s\n ", strerror(errno));
          }
          else
          {
            if (write(STDOUT_FILENO, &buffer[i], sizeof(char)) < 0)
              fprintf(stderr, "ERROR: Bad write to stdout, error number: %s\n ", strerror(errno));
          }
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
          {"log", required_argument, 0, 'l'},
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
      logFlag = 1;
      logFile = optarg;
      logfd = creat(logFile, S_IRWXU);
      //logfd = open(logFile, O_RDWR | O_APPEND | O_CREAT, 0666);
      if (logfd < 0)
      {
        fprintf(stderr, "ERROR: Reading or writing to log, error number: %s\n", strerror(errno));
        exit(1);
      }
      // if (optarg != NULL)
      //   logFile = optarg;
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

  createSocket();
  newWriteSocket();

  exit(0);
}
