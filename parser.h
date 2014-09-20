#ifndef FILE_PARSER
#define FILE_PARSER

#define MAXARGUS 10

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGUS];
};

struct redircmd {
  int type;
  struct cmd* cmd;
  char *file; // input/ooutput name
  int mode;
  int fd;  // file descriptor number
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

int get_tokens(const char *cmdline, char** argv);
int is_blank(char c);
int is_delim(char c);
int is_background(char** argv);
void token_dump(char** argv);
void token_clear(char** argv);
void cmd_dump(struct cmd* cmd);

int fork1();
int peek(int *no, char** argv, char *str);
struct cmd* parsecmd(char** argv);
struct cmd* parseline(int *no, char** argv);
struct cmd* parsepipe(int *no, char** argv);
struct cmd* parseexec(int *no, char** argv);
struct cmd* parseredirs(struct cmd *cmd, int *no, char** argv);
struct cmd* make_cmd(void);
struct cmd* make_redircmd(struct cmd *subcmd, char *file, int type);
struct cmd* make_pipecmd(struct cmd *left, struct cmd *right);
#endif
