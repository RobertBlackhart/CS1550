/**
 * Projec1
 * myshell.c
 * 5/31/13
 * Robert Mcdermot (rom66)
 * compile: gcc -o myshell myshell.c lex.yy.c -lfl
 * run: ./myshell
 **/
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

extern char **parseline();
void runCommand(char** args, int redirect, int numPipes);
int containsRedirect(char** args);
int containsPipe(char** args);
void execute(char** args, int redirect);
char** getCommand(char** args, int commandNum);

char* filename;

int main() 
{
	int i;
	char **args;

	while(1) 
	{
		printf("myshell> ");
		
		args = parseline();

		if(strcmp(args[0],"exit") == 0)
			exit(EXIT_SUCCESS);
		else if(strcmp(args[0],"cd") == 0)
		{
			if(chdir(args[1]) == -1)
				printf("Changing directory failed: %s\n", strerror(errno));
		}		
		else 
		{
			if(containsRedirect(args) == 1)
			{
				for(i=0; args[i] != NULL; i++)
				{
					if(strcmp(args[i],">") == 0)
					{
						filename = args[i+1];
						break;
					}
				}
				int size = i;
				char** a = malloc(sizeof(char*)*size);
				for(i=0; i<size; i++)
				{
					a[i] = args[i];
				}
				runCommand(a,1,0);
				free(a);
			}
			else if(containsPipe(args) > 0)
			{
				runCommand(args,0,containsPipe(args));
			}
			else
			{
				runCommand(args,0,0);
			}
		}
	}

	return 0;
}

void runCommand(char** args, int redirect, int numPipes)
{
	if(redirect)
	{
		execute(args,1);
	}
	else if(numPipes > 0)
	{
		int i, in = 0, pid, status;
		int fd[2];
		
		for(i = 0; i<numPipes+1; i++)
		{
			pipe(fd);
			
			if((pid = fork()) == 0)
			{
				char** command = getCommand(args,i);
				
				dup2 (in, STDIN_FILENO);
				if(i < numPipes) //commands 0...n-1
					dup2 (fd[1], STDOUT_FILENO);
				
				if(i == numPipes) //command n
					dup2(1, STDOUT_FILENO);
					
				if(execvp(command[0],command) == -1)
				{
					printf("%s: %s\n", command[0],strerror(errno));
					exit(EXIT_FAILURE);
				}
				else
					exit(EXIT_SUCCESS);
					
			}
			
			close(fd[1]);
			in = fd[0];
		}
		
		waitpid(pid,&status,0);
	}
	else
		execute(args,0);
}

char** getCommand(char** args, int commandNum)
{
	char** command;
	int i, j, pipeFound = 0, lastPipeIndex=0;
	for(i = 0; args[i] != NULL; i++)
	{
		if(strcmp(args[i],"|") == 0)
		{
			if(pipeFound == commandNum)
			{
				command = malloc(sizeof(char*)*(i-lastPipeIndex));
				for(j=lastPipeIndex; j<i; j++)
				{
					command[j-lastPipeIndex] = args[j];
				}
				return command;
			}
			pipeFound++;
			lastPipeIndex = i+1;
		}
	}
	
	command = malloc(sizeof(char*)*(i-lastPipeIndex));
	for(j=lastPipeIndex; j<i; j++)
	{
		command[j-lastPipeIndex] = args[j];
	}
	return command;
}

void execute(char** args, int redirect)
{
	pid_t pID = fork();
	if (pID == -1)
		printf("Unable to create new process: %s\n", strerror(errno));
	else if (pID == 0) //this is the child
	{
		if(redirect)
		{
			if(!freopen(filename, "w", stdout))
			{
				printf("Unable to redirect output to %s: %s\n",filename,strerror(errno));
				exit(EXIT_FAILURE);
			}
		}
		
		if(execvp(args[0],args) == -1)
		{
			printf("%s: %s\n", args[0],strerror(errno));
			exit(EXIT_FAILURE);
		}
		else
			exit(EXIT_SUCCESS);
	}
	else //this is the parent
	{
		int status;
		pid_t result = waitpid(pID,&status,0);
		if(result == -1)
			printf("Child process failed: %s\n", strerror(errno));
	}
}

int containsRedirect(char** args)
{
	int i;
	
	for(i = 0; args[i] != NULL; i++) 
	{
		if(strcmp(args[i],">") == 0)
			return 1;
	}
	
	return 0;
}

int containsPipe(char** args)
{
	int i,numPipes=0;
	
	for(i = 0; args[i] != NULL; i++) 
	{
		if(strcmp(args[i],"|") == 0)
			numPipes++;
	}
	
	return numPipes;
}
