%{
#define YY_DECL char **yylex()
int _numargs = 10;
char *_args[10];
int _argcount = 0;
%}

WORD	[a-zA-Z0-9\/\.-]+
SPECIAL	[()><|&;*]

%%
	_argcount = 0; 
	_args[0] = NULL; 

{WORD}|{SPECIAL} {  
	  if(_argcount < _numargs-1) {
	    _args[_argcount++] = (char *)strdup(yytext);
	    _args[_argcount] = NULL;
	  }
	}

\n	return _args;

[ \t]+

.

%%

char **parseline() {
  return (char **)yylex();
}
