#include "../header/functions.h"

char mode_start;

void write_in_queue(RT_QUEUE *, MessageToMon);

void f_server(void *arg) {
    int err;
    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);

    err = run_nodejs("/usr/local/bin/node", "/home/pi/Interface_Robot/server.js");

    if (err < 0) {
        printf("Failed to start nodejs: %s\n", strerror(-err));
        exit(EXIT_FAILURE);
    } else {
#ifdef _WITH_TRACE_
        printf("%s: nodejs started\n", info.name);
#endif
        open_server();
        rt_sem_broadcast(&sem_serverOk);
    }
}

void f_sendToMon(void * arg) {
    int err;
    MessageToMon msg;

    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);

#ifdef _WITH_TRACE_
    printf("%s : waiting for sem_serverOk\n", info.name);
#endif
    rt_sem_p(&sem_serverOk, TM_INFINITE);
    while (1) {

#ifdef _WITH_TRACE_
        printf("%s : waiting for a message in queue\n", info.name);
#endif
        if (rt_queue_read(&q_messageToMon, &msg, sizeof (MessageToRobot), TM_INFINITE) >= 0) {
#ifdef _WITH_TRACE_
            printf("%s : message {%s,%s} in queue\n", info.name, msg.header, msg.data);
#endif

            send_message_to_monitor(msg.header, msg.data);
            free_msgToMon_data(&msg);
            rt_queue_free(&q_messageToMon, &msg);
        } else {
            printf("Error msg queue write: %s\n", strerror(-err));
        }
    }
}

void f_receiveFromMon(void *arg) {
    MessageFromMon msg;
    int err;

    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);

#ifdef _WITH_TRACE_
    printf("%s : waiting for sem_serverOk\n", info.name);
#endif
    rt_sem_p(&sem_serverOk, TM_INFINITE);
    do {
#ifdef _WITH_TRACE_
        printf("%s : waiting for a message from monitor\n", info.name);
#endif
        err = receive_message_from_monitor(msg.header, msg.data);
#ifdef _WITH_TRACE_
        printf("%s: msg {header:%s,data=%s} received from UI\n", info.name, msg.header, msg.data);
#endif
        if (strcmp(msg.header, HEADER_MTS_COM_DMB) == 0) {
            if (msg.data[0] == OPEN_COM_DMB) { // Open communication supervisor-robot
#ifdef _WITH_TRACE_
                printf("%s: message open Xbee communication\n", info.name);
#endif
                rt_sem_v(&sem_openComRobot);
            }
        } else if (strcmp(msg.header, HEADER_MTS_DMB_ORDER) == 0) {
            if (msg.data[0] == DMB_START_WITHOUT_WD) { // Start robot
#ifdef _WITH_TRACE_
                printf("%s: message start robot\n", info.name);
#endif 
                rt_sem_v(&sem_startRobot);

            } else if ((msg.data[0] == DMB_GO_BACK)
                    || (msg.data[0] == DMB_GO_FORWARD)
                    || (msg.data[0] == DMB_GO_LEFT)
                    || (msg.data[0] == DMB_GO_RIGHT)
                    || (msg.data[0] == DMB_STOP_MOVE)) {

                rt_mutex_acquire(&mutex_move, TM_INFINITE);
                move = msg.data[0];
                rt_mutex_release(&mutex_move);
#ifdef _WITH_TRACE_
                printf("%s: message update movement with %c\n", info.name, move);
#endif

            }
        }
    } while (err > 0);

}

void f_openComRobot(void * arg) {
    int err;

    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);

    while (1) {
#ifdef _WITH_TRACE_
        printf("%s : Wait sem_openComRobot\n", info.name);
#endif
        rt_sem_p(&sem_openComRobot, TM_INFINITE);
#ifdef _WITH_TRACE_
        printf("%s : sem_openComRobot arrived => open communication robot\n", info.name);
#endif
        err = open_communication_robot();
        if (err == 0) {
#ifdef _WITH_TRACE_
            printf("%s : the communication is opened\n", info.name);
#endif
            MessageToMon msg;
            set_msgToMon_header(&msg, HEADER_STM_ACK);
            write_in_queue(&q_messageToMon, msg);
        } else {
            MessageToMon msg;
            set_msgToMon_header(&msg, HEADER_STM_NO_ACK);
            write_in_queue(&q_messageToMon, msg);
        }
    }
}

void f_startRobot(void * arg) {
    int err;

    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);

    while (1) {
#ifdef _WITH_TRACE_
        printf("%s : Wait sem_startRobot\n", info.name);
#endif
        rt_sem_p(&sem_startRobot, TM_INFINITE);
#ifdef _WITH_TRACE_
        printf("%s : sem_startRobot arrived => Start robot\n", info.name);
#endif
        
        rt_mutex_acquire(&mutex_WD,TM_INFINITE);
        if( WD == 1 ) { // WatchDog ON
            err = send_command_to_robot(DMB_START_WITH_WD);
        }
        else {
            err = send_command_to_robot(DMB_START_WITHOUT_WD);
        }

        if (err == 0) {
#ifdef _WITH_TRACE_
            printf("%s : the robot is started\n", info.name);
#endif
            rt_mutex_acquire(&mutex_robotStarted, TM_INFINITE);
            robotStarted = 1;
            rt_mutex_release(&mutex_robotStarted);
            MessageToMon msg;
            set_msgToMon_header(&msg, HEADER_STM_ACK);
            write_in_queue(&q_messageToMon, msg);
            if (WD == 1)
                rt_sem_v(&sem_withWD);
        } else {
            MessageToMon msg;
            set_msgToMon_header(&msg, HEADER_STM_NO_ACK);
            write_in_queue(&q_messageToMon, msg);
        }
        rt_mutex_release(&mutex_WD);
    }
}

void f_move(void *arg) {
    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);

    /* PERIODIC START */
#ifdef _WITH_TRACE_
    printf("%s: start period\n", info.name);
#endif
    rt_task_set_periodic(NULL, TM_NOW, 100000000);
    while (1) {
#ifdef _WITH_TRACE_
        printf("%s: Wait period \n", info.name);
#endif
        rt_task_wait_period(NULL);
#ifdef _WITH_TRACE_
        printf("%s: Periodic activation\n", info.name);
        printf("%s: move equals %c\n", info.name, move);
#endif
        rt_mutex_acquire(&mutex_robotStarted, TM_INFINITE);
        if (robotStarted) {
            
            int err;
            
            rt_mutex_acquire(&mutex_move, TM_INFINITE);
            err = send_command_to_robot(move);
            rt_mutex_release(&mutex_move);
            
            rt_mutex_acquire(&mutex_cpt_err,TM_INFINITE);
            if (err == ROBOT_OK ){ // Pas d'erreur (ROBOT_OK) 
                #ifdef _WITH_TRACE_
                printf("%s: the movement %c was sent\n", info.name, move);
                #endif         
                cpt_err = 0;
            }
            else if (err == ROBOT_TIMED_OUT || err == ROBOT_UKNOWN_CMD  || err == ROBOT_ERROR || err == ROBOT_CHECKSUM ) {
                cpt_err++;
                #ifdef _WITH_TRACE_
                printf("%s: Cpt erreur = %d\n",info.name,cpt_err);
                #endif
                if (cpt_err == 3) {
                    robotStarted = 0;
                    MessageToMon msg;
                    set_msgToMon_header(&msg,HEADER_STM_LOST_DMB);
                    write_in_queue(&q_messageToMon,msg);
                    close_communication_robot;
                    set_msgToMon_header(&msg,HEADER_STM_MES);
                    char* Msg_Lost_Co = "Perte de connexion avec le robot\n";
                    set_msgToMon_data(&msg,Msg_Lost_Co);
                    write_in_queue(&q_messageToMon,msg);
                    cpt_err = 0;
                }
            }
            rt_mutex_release(&mutex_cpt_err);
   
        }
        rt_mutex_release(&mutex_robotStarted);
    }
}

void f_gestionBatterie(void *arg) {
    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);
    
    /* PERIODIC START */
#ifdef _WITH_TRACE_
    printf("%s: start period\n", info.name);
#endif
    rt_task_set_periodic(NULL, TM_NOW, 500000000);
    while (1) {
#ifdef _WITH_TRACE_
        printf("%s: Wait period \n", info.name);
#endif
        rt_task_wait_period(NULL);
#ifdef _WITH_TRACE_
        printf("%s: Periodic activation\n", info.name);

#endif
        rt_mutex_acquire(&mutex_robotStarted, TM_INFINITE);
        if (robotStarted) {
            
            int bat;
            bat = send_command_to_robot(DMB_GET_VBAT);
            
            rt_mutex_acquire(&mutex_cpt_err,TM_INFINITE);
            if (bat >= 0 ){ //bat = (DMB_BAT_LOW||DMB_BAT_MED||DMB_BAT_HIGH)
                MessageToMon msg;
                // bat = 2; // On teste l'affichage en forçant.
                bat += 48;
                set_msgToMon_header(&msg,HEADER_STM_BAT);
                set_msgToMon_data(&msg,&bat);
                write_in_queue(&q_messageToMon, msg);
                cpt_err = 0;
                #ifdef _WITH_TRACE_
                printf("%s: the battery level %c was sent\n", info.name, bat);
                #endif
            }
            else if (bat == ROBOT_TIMED_OUT || bat == ROBOT_UKNOWN_CMD  || bat == ROBOT_ERROR || bat == ROBOT_CHECKSUM )  {
                cpt_err++;
                #ifdef _WITH_TRACE_
                printf("%s: Cpt erreur = %d\n",info.name,cpt_err);
                #endif
                if (cpt_err == 3) {
                    robotStarted = 0;
                    MessageToMon msg;
                    set_msgToMon_header(&msg,HEADER_STM_LOST_DMB);
                    write_in_queue(&q_messageToMon,msg);
                    close_communication_robot;
                    char* Msg_Lost_Co = "Perte de connexion avec le robot\n";
                    set_msgToMon_data(&msg,Msg_Lost_Co);
                    write_in_queue(&q_messageToMon,msg);
                    cpt_err = 0;
                }
            }
            rt_mutex_release(&mutex_cpt_err);        
        }
        rt_mutex_release(&mutex_robotStarted);
    }  
}


void f_gestionWatchDog(void *arg) { //Pas faite
    /* INIT */
    RT_TASK_INFO info;
    rt_task_inquire(NULL, &info);
    printf("Init %s\n", info.name);
    rt_sem_p(&sem_barrier, TM_INFINITE);
    
    /* PERIODIC START */
#ifdef _WITH_TRACE_
    printf("%s: start period\n", info.name);
#endif
    rt_task_set_periodic(NULL, TM_NOW, 1000000000);
    rt_sem_p(&sem_withWD,TM_INFINITE);
    
    while (1) {
#ifdef _WITH_TRACE_
        printf("%s: Wait period \n", info.name);
#endif
        rt_task_wait_period(NULL);
#ifdef _WITH_TRACE_
        printf("%s: Periodic activation\n", info.name);

#endif
        rt_mutex_acquire(&mutex_robotStarted, TM_INFINITE);
        if (robotStarted) {
            
            int err;
            err = send_command_to_robot(DMB_RELOAD_WD);
            
            rt_mutex_acquire(&mutex_cpt_err,TM_INFINITE);
            if (err == ROBOT_OK ){ // Pas d'erreur 
#ifdef _WITH_TRACE_
            printf("%s: the watchdog reload order was sent\n", info.name);
#endif     
                cpt_err = 0;
            }
            else if (err == ROBOT_TIMED_OUT || err == ROBOT_UKNOWN_CMD  || err == ROBOT_ERROR || err == ROBOT_CHECKSUM )  {
                cpt_err++;
                #ifdef _WITH_TRACE_
                printf("%s: Cpt erreur = %d\n",info.name,cpt_err);
                #endif
                if (cpt_err == 3) {
                    robotStarted = 0;
                    MessageToMon msg;
                    set_msgToMon_header(&msg,HEADER_STM_LOST_DMB);
                    write_in_queue(&q_messageToMon,msg);
                    close_communication_robot;
                    char* Msg_Lost_Co = "Perte de connexion avec le robot\n";
                    set_msgToMon_data(&msg,Msg_Lost_Co);
                    write_in_queue(&q_messageToMon,msg);
                    cpt_err = 0;
                }
            }
            rt_mutex_release(&mutex_cpt_err);
       
        }
        rt_mutex_release(&mutex_robotStarted);
    }  
}


void write_in_queue(RT_QUEUE *queue, MessageToMon msg) {
    void *buff;
    buff = rt_queue_alloc(&q_messageToMon, sizeof (MessageToMon));
    memcpy(buff, &msg, sizeof (MessageToMon));
    rt_queue_send(&q_messageToMon, buff, sizeof (MessageToMon), Q_NORMAL);
}


void ReInit(void *arg){

	while (1) {
#ifdef _WITH_TRACE_
printf("%s: Wait for rst \n", info.name);
#endif
		rt_sem_p(&sem_rst, TM_INFINITE);
#ifdef _WITH_TRACE_
printf("%s: rst on, stop nodejs and server\n", info.name);
#endif
		
		kill_nodejs();
		close_server();

		printf(«%s: Nodejs is lost \n », info.name);

		rt_mutex_acquire(&mutex_robotStarted, TM_INFINITE);
		robotStarted = 0;
		rt_mutex_release(&mutex_robotStarted);
                
		rt_mutex_acquire(&mutex_Camera, TM_INFINITE);
		Camera = 0;
        rt_mutex_release(&mutex_Camera);

	}
}

void f_DetecterArene(void *arg){

	while (1) {
		rt_mutex_acquire(&mutex_rechercheArene, TM_INFINITE);
		if (rechercheArene = 1) {
			get_image();
			
			rt_mutex_acquire(&mutex_Arena, TM_INFINITE);
			arena = detect_arena(image);
			
			if (arena = NULL) {
				printf("%s: Arene non trouvée\n");
			} else {
				draw_arena(image,arena);
				jpgimage = compress_image(image);
				//message tomon
			}
			rechercheArene = 0;
			rt_mutex_release(&mutex_Arena);
			rt_mutex_release(&mutex_rechercheArene);
		}
	}
}