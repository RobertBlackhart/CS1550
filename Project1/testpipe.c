#include <stdio.h>
#include <unistd.h>

int main()
{
	char *ls[] = {"ls","-alh",NULL};
	char *wc[] = {"wc", "-l", NULL};

	int fd[2];

	int pidls, pidwc;

	if((pidls = fork()) == 0)
	{
		pipe(fd);
		
		if((pidwc = fork()) == 0)
		{
			close(fd[0]);
			dup2(fd[1], 1);
			close(fd[1]);
			execvp(ls[0], ls);
			
		}
		else
		{
			close(fd[1]);
			dup2(fd[0],0);
			close(fd[0]);		
			execvp(wc[0], wc);
		}
	}
	else
	{
			int status;
			wait(&status);
	}

	return 0;
}
