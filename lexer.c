#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>

enum states {
  STATE_BASE = 0,
  STATE_STRING,
  STATE_NUMBER,
  STATE_TRUE,
  STATE_FALSE,
  STATE_NULL,
  STATE_STRING_SPECCHAR
};

enum restype {
  RES_NUMBER = 0,
  RES_STRING,
  RES_TRUE,
  RES_FALSE,
  RES_NULL,

  RES_OPEN_BRACE,
  RES_CLOSE_BRACE,
  RES_OPEN_BRACKET,
  RES_CLOSE_BRACKET,

  RES_STRING_PARTIAL,
  RES_NUMBER_PARTIAL
};

#define RESULT_COUNT 10000

struct lexer_result {
  int restype;
  int startpos;
  int length;

  int adddata; // Additional data to result
};

struct lexer {
  enum states current_state;
  int position;
  int length;

  int state_data;
  int state_data_2;

  int result_num;
  struct lexer_result result[RESULT_COUNT];
};

typedef enum {
  LEX_OK = 0,
  LEX_YIELD = 1,
  LEX_ERROR = 2
} resstate;

static inline int isempty(char chr) {
  return (chr == ':' || chr == ',' || isspace(chr));
}

static inline int isJnumber(char chr) {
  return ((chr >= '0' && chr <= '9') || chr == '-' || chr == '.' || chr == '+' || chr == 'e' || chr == 'E');
}

// Add simple result to the result list
static inline void add_simple_res(enum restype restype, struct lexer *lexer) {
  struct lexer_result *res = &lexer->result[lexer->result_num];

  res->restype = restype;
  res->startpos = lexer->position;
  res->length = 0;
  lexer->result_num++;
}

static inline resstate handle_space(const char *input, struct lexer *lexer) {
  /* Skip space */
  while (lexer->position < lexer->length && isempty(input[lexer->position]))
    lexer->position++;

  if (lexer->position >= lexer->length)
    return LEX_YIELD;

  return LEX_OK;
}

static inline resstate handle_base(const char *input, struct lexer *lexer) {
  if (handle_space(input, lexer))
    return LEX_OK;

  char chr = input[lexer->position];
  switch (chr) {
    case '{': add_simple_res(RES_OPEN_BRACE, lexer); lexer->position++;break;
    case '}': add_simple_res(RES_CLOSE_BRACE, lexer); lexer->position++;break;
    case '[': add_simple_res(RES_OPEN_BRACKET, lexer); lexer->position++;break;
    case ']': add_simple_res(RES_CLOSE_BRACKET, lexer); lexer->position++;break;
    case '"': lexer->current_state = STATE_STRING; lexer->position++;return LEX_OK;
    case 't': lexer->current_state = STATE_TRUE; lexer->state_data = 1; lexer->position++;return LEX_OK;
    case 'f': lexer->current_state = STATE_FALSE; lexer->state_data = 1; lexer->position++;return LEX_OK;
    case 'n': lexer->current_state = STATE_NULL; lexer->state_data = 1; lexer->position++;return LEX_OK;
    default:
      if (isJnumber(chr)) {
        lexer->current_state = STATE_NUMBER;
        lexer->state_data = 0;
        return LEX_OK;
      } else {
        printf("Uknown character: %c\n", chr);
        return LEX_ERROR;
      }
  }
  return LEX_OK;
}

static inline resstate handle_ident(const char *input, struct lexer *lexer, const char *ident, enum restype idtype) {
  while (lexer->position < lexer->length) {
    char chr = input[lexer->position];
    if (!ident[lexer->state_data]) {
      // Check that the next character is allowed
      if (isempty(chr) || chr == ']' || chr == '}') {
        add_simple_res(idtype, lexer);
        lexer->current_state = STATE_BASE;
        return LEX_OK;
      } else {
        printf("Unexpected next character in handle_ident: %d\n", chr);
        return LEX_ERROR;
      }
    }
    if (ident[lexer->state_data] != chr)
      return LEX_ERROR;
    lexer->state_data++;
    lexer->position++;
  }
  return LEX_OK;
}

resstate handle_number(const char *input, struct lexer *lexer) {
  /* Just eat characters that can be numbers and feed them to a table */
  // Copy the character to buffer
  int startposition = lexer->position;
  for (;lexer->position < lexer->length && isJnumber(input[lexer->position]);++lexer->position)
       ;

  struct lexer_result *res = &lexer->result[lexer->result_num];
  if (lexer->position == lexer->length) {
    res->restype = RES_NUMBER_PARTIAL;
  } else {
    res->restype = RES_NUMBER;
    lexer->current_state = STATE_BASE;
  }
  // We can just point directly to the input
  res->adddata = 0;
  res->startpos = startposition;
  res->length = lexer->position - startposition;

  lexer->result_num++;
  return LEX_OK;
}

static inline int safechar(char x) {
  return (x != '"' && x != '\\');
}

/* Handle beginning of a string, the '"' is already stripped */
resstate handle_string(const char *input, struct lexer *lexer) {
    int startposition = lexer->position;
    for (char ch=input[lexer->position]; lexer->position < lexer->length && safechar(ch); ch = input[++lexer->position])
      ;

    struct lexer_result *res = &lexer->result[lexer->result_num];
    res->startpos = startposition;
    res->length = lexer->position - startposition;
    if (lexer->position == lexer->length || input[lexer->position] == '\\') {
      // Emit partial string
      res->restype = RES_STRING_PARTIAL;
      if (res->length) // Skip, if the string is empty
          lexer->result_num++;
      // If we stopped because of backslash, change state, move one forward
      if (lexer->position < lexer->length) {
          lexer->current_state = STATE_STRING_SPECCHAR;
          lexer->position++;
      }
      return LEX_OK;
    } else if (input[lexer->position] == '"') {
      res->restype = RES_STRING;
      // We can just point directly to the input
      lexer->result_num++;
      lexer->current_state = STATE_BASE;
      lexer->position++; // Skip the final '"'
      return LEX_OK;
    }
    printf("Internal error.\n");
    return LEX_ERROR;
}

// Add a character to result, move position forward, change state back to string
static inline void emitchar(char ch, struct lexer *lexer) {
  struct lexer_result *res = &lexer->result[lexer->result_num];

  res->restype = RES_STRING_PARTIAL;
  res->startpos = lexer->position;
  res->length = 0;
  res->adddata = ch;

  lexer->result_num++;
  lexer->position++;
  lexer->current_state = STATE_STRING;
}

resstate handle_specchar(const char *input, struct lexer *lexer) {
  char chr = input[lexer->position];
  switch (chr) {
    case '"': emitchar('"', lexer);break;
    case '\\':emitchar('\\', lexer);break;
    case '/':emitchar('/', lexer);break;
    case 'b':emitchar('\b', lexer);break;
    case 'f':emitchar('\f', lexer);break;
    case 'n':emitchar('\n', lexer);break;
    case 'r':emitchar('\r', lexer);break;
    case 't':emitchar('\t', lexer);break;
//    case 'u':
    default:
      return LEX_ERROR;
  }
  return LEX_OK;
}

resstate lexit(const char *input, struct lexer *lexer)
{
  resstate res = LEX_OK;
  static void* dispatch_table[] = {
      &&state_base, &&state_string, &&state_number, &&state_true,
      &&state_false, &&state_null, &&state_string_specchar
  };
  #define DISPATCH() { \
     if (!(lexer->position < lexer->length && lexer->result_num < RESULT_COUNT && res == 0)) \
        return res; \
     goto *dispatch_table[lexer->current_state];\
     }

  DISPATCH();
  state_base:
    res = handle_base(input, lexer);
    DISPATCH();
  state_string:
    res = handle_string(input, lexer);
    DISPATCH();
  state_number:
    res = handle_number(input, lexer);
    DISPATCH();
  state_true:
    res = handle_ident(input, lexer, "true", RES_TRUE);
    DISPATCH();
  state_false:
    res = handle_ident(input, lexer, "false", RES_FALSE);
    DISPATCH();
  state_null:
    res = handle_ident(input, lexer, "null", RES_NULL);
    DISPATCH();
  state_string_specchar:
    res = handle_specchar(input, lexer);
    DISPATCH();

  return res;
}

char *test1 = "{}";
char *test2 = " [ [ true, false, null, 256, \"ond\\n\\nra\", \"martin\", -3.14e+12  ]]   ";

void printres(const char *input, struct lexer_result *res) {
  if (res->length) {
      printf("Inline: ");
      for (int j=res->startpos; j < res->startpos + res->length;j++) {
        printf("%c", input[j]);
      }
  } else {
      printf("Additional data: %d", res->adddata);
  }
  printf("\n");
}

int test(char *input) {
  struct lexer lexer;
  lexer.current_state = STATE_BASE;
  lexer.position = 0;
  lexer.length = strlen(input);
  while (lexer.position < lexer.length) {
  // for (;*input;input++) {
      // lexer.position = 0;
      // lexer.length = 1;
      lexer.result_num = 0;

      int res = lexit(input, &lexer);
      if (res == LEX_ERROR) {
        printf("LEX ERROR\n");
        break;
      }
      if (lexer.result_num)
        printf("Count of results: %d\n", lexer.result_num);
      for (int i=0; i < lexer.result_num; i++) {
        struct lexer_result *res = &lexer.result[i];
        printf("Item: TYPE: %d POS: %d, LEN: %d\n",
                res->restype, res->startpos, res->length);
        if (res->restype == RES_NUMBER || res->restype == RES_NUMBER_PARTIAL) {
            printres(input, res);
        } else if (res->restype == RES_STRING || res->restype == RES_STRING_PARTIAL) {
            printres(input, res);
        }
      }
  }
  return 0;
}

void speedtest(int fd)
{
    int counter = 0;
    const int bufsize = 32768;
    char buffer[bufsize];
    struct lexer lexer;

    int size = read(fd, buffer, bufsize);
    for (int i=0; i< 50000; i++) {
        lexer.current_state = STATE_BASE;
        lexer.position = 0;
        lexer.length = size;

        while (lexer.position < lexer.length) {
          lexer.result_num = 0;
          int res = lexit(buffer, &lexer);
          if (res) {
            printf("Error\n");
            return;
          }
        }
    }
    printf("Total: %d\n", counter);
}

int main(void) {
  test(test2);
  // int fd = open("test.json", O_RDONLY);
  // if (fd == -1) {
  //   perror("File");
  //   return 0;
  // }
  // speedtest(fd);
}
