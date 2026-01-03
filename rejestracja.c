


//  1. obluga dwoch okienek 



/*
    definiujemy z jakiego okienka korzystamy

    podlaczyc do wszystkich IPC

    pam_dziel - czy okienko dwa otwarte tak/nie

    sem dla pam_dziel

    kolejka wejsciowa - skad bierzemy pacjenta

    kolejka wyjsciowa czyli lekarz poz 



    int nr okienka =  argv[1]



    void handle_sig(nr okienka)


    shmid, semid,

    msgid_we = (id koelejka rejestracji)
    msgid_wy = (id kolejka poz)

    while 1:

    lock
    otwarte? = stan->czyokienkotwarte  
    dlugosc kol = stan-dlkol

    if(!otwarte?)
    sleep(1)
    continue


    if dlkolej < maxpacj / 3

    stan->czyokno2otwarte = 0
    unlock
    continue


    msgrcv(z recepcji)

    lock
    stan->koejka--
    unlock

    msgsnd(do lekarza poz)

    



*/