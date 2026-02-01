


void* funckja_watku(void * arg)
{
    int suma_miejsc = 0;
    int status;
    pid_t pid;
    

    while(czyszczenie)
    {
        pid = waitpid(-1, &status, WNOHANG)) > 0) 
         if (WIFEXITED(status)) {
                suma_miejsc += WEXITSTATUS(status);
            }
        
        if (pid == -1 && errno == ECHILD) break;

        struct sembuf unlock = {SEM_GENERATOR, 1, SEM_UNDO};
            semop(semid, &unlock, 1);
    }
}



for()
{


    //generowanie
}



