/******************************************************************************
 *  bwm-ng                                                                    *
 *                                                                            *
 *  Copyright (C) 2004 Volker Gropp (bwmng@gropp.org)                         *
 *                                                                            *
 *  for more info read README.                                                *
 *                                                                            *
 *  This program is free software; you can redistribute it and/or modify      *
 *  it under the terms of the GNU General Public License as published by      *
 *  the Free Software Foundation; either version 2 of the License, or         *
 *  (at your option) any later version.                                       *
 *                                                                            *
 *  This program is distributed in the hope that it will be useful,           *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
 *  GNU General Public License for more details.                              *
 *                                                                            *
 *  You should have received a copy of the GNU General Public License         *
 *  along with this program; if not, write to the Free Software               *
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA *
 *                                                                            *
 *****************************************************************************/

#include "bwm-ng.h"

/* chooses the correct get_iface_stats() to use */
inline void get_iface_stats(char _n) {
	switch (input_method) { 
#ifdef NETSTAT		
	    case NETSTAT_IN: 
			get_iface_stats_netstat(_n); 
	        break; 
#endif			
#ifdef LIBSTATGRAB 
		case LIBSTAT_IN: 
            get_iface_stats_libstat(_n); 
            break; 
#endif 
#ifdef PROC_NET_DEV 
	    case PROC_IN: 
            get_iface_stats_proc(_n); 
            break; 
#endif 
#ifdef GETIFADDRS 
	    case GETIFADDRS_IN: 
            get_iface_stats_getifaddrs(_n); 
            break; 
#endif 
#ifdef SYSCTL
        case SYSCTL_IN:
            get_iface_stats_sysctl(_n);
            break;
#endif
#if HAVE_LIBKSTAT
        case KSTAT_IN:
            get_iface_stats_kstat(_n);
            break;
#endif
			
	}
}


/* clear stuff and exit */
#ifdef __STDC__
void deinit(char *error_msg, ...) FUNCATTR_NORETURN;
void deinit(char *error_msg, ...) {
#else
void deinit(...) FUNCATTR_NORETURN;
void deinit(...) {
#endif
    va_list    ap;
#if EXTENDED_STATS
    int local_if_count;
    struct double_list *list_p;
#endif    
#ifdef HAVE_CURSES	
	if ((output_method==CURSES_OUT || output_method==CURSES2_OUT) && myscr!=NULL) {
		/* first close curses, so we dont leave mess behind */
#if HAVE_CURS_SET
        curs_set(1);
#endif
		endwin();
        delscreen(myscr);
	}
#endif	
#ifdef IOCTL
	/* close socket if we opened it for ioctl */
	if (skfd >= 0) { 
		close(skfd);
	}
#endif	
	/* we should clean if_state, the data array */
	if (if_stats!=NULL) {
#if EXTENDED_STATS        
        /* clean avg list for each iface */
        for (local_if_count=0;local_if_count<if_count;local_if_count++) {
            /* free the name */
            free(if_stats[local_if_count].if_name);
            while (if_stats[local_if_count].avg.first!=NULL) {
                list_p=if_stats[local_if_count].avg.first;
                if_stats[local_if_count].avg.first=if_stats[local_if_count].avg.first->next;
                free(list_p);
            }
        }
#endif
        free(if_stats);
    }
	/* free the opt iface_list, ifaces to show or hide */
	if (iface_list!=NULL) free(iface_list);
#if CSV || HTML
	/* close the out_file */
	if (out_file!=NULL) fclose(out_file);
    if (out_file_path!=NULL) free(out_file_path);
#endif	
#ifdef __STDC__        
    /* output errormsg if given */
    if (error_msg!=NULL) {
        va_start(ap, error_msg);
		vprintf(error_msg,ap);
    }
#else
    va_start(ap);
    vprintf(ap);
#endif
	/* we are done, say goodbye */
    exit(0);
}


/* handle interrupt signal */
void sigint(int sig) FUNCATTR_NORETURN;

/* sigint handler */
void sigint(int sig) {
	/* we got a sigint, call deinit and exit */
	deinit(NULL);
}


/* do the main thing */
int main (int argc, char *argv[]) {
	unsigned char idle_chars_p=0;
	char ch;

#ifdef PROC_NET_DEV 
	strcpy(PROC_FILE,PROC_NET_DEV);
#endif
    
    /* handle all cmd line and configfile options */
	get_cmdln_options(argc,argv);
    /* check them */
    if (output_method<0)
        deinit("invalid output selected\n");
    if (input_method<0)
        deinit("invalid input selected\n");
    
    /* init total stats to zero */
	memset(&if_stats_total,0,(size_t)sizeof(t_iface_stats));
#ifdef HAVE_CURSES
	if (output_method==CURSES_OUT || output_method==CURSES2_OUT) {
		/* init curses */
        if (init_curses())
            signal(SIGWINCH,sigwinch);
	}
#endif	
	/* end of init curses, now set a sigint handler to deinit the screen on ctrl-break */
	signal(SIGINT,sigint);
	signal(SIGTERM,sigint);
#ifdef CSV	
    /* get stats without verbose if cvs */
	if (output_method==CSV_OUT && output_count>-1) {
		get_iface_stats(0);
		usleep(delay*1000);
	}
#endif	
	if (daemonize) {
	  	int nbyt = 0;
	  	/* lets fork into background */
		if ((nbyt = fork()) == -1) {
			deinit("could not fork into background: %s\n",strerror(errno));
		}
		if (nbyt != 0) { /* nbyt is the new child pid here */
			deinit("forking into background\n");
		}
		setsid();
	}
	if (output_count==0) output_count=-1;
	if (output_method==PLAIN_OUT && output_count==1) output_method=PLAIN_OUT_ONCE;
	if (output_method==PLAIN_OUT) printf("\033[2J"); /* clear screen for plain out */
    while (1) { /* do the main loop */
#ifdef HTML
        /* open the output file */
        if (output_method==HTML_OUT && out_file_path) {
            if (out_file) fclose(out_file);
            out_file=fopen(out_file_path,"w");
        }
#endif
        /* check if we will output anything */
        ch=!(output_method==PLAIN_OUT_ONCE
#ifdef HTML
                    || (output_method==HTML_OUT && !daemonize)
#endif
            ); 

        /* print the header (info) if verbose */
		if (ch) idle_chars_p=print_header(idle_chars_p); 
        /* do the actual work, get and print stats */
		get_iface_stats(ch); 

#if HTML
        /* close html tags */
        if (output_method==HTML_OUT && html_header && daemonize)
            fprintf((out_file==NULL ? stdout : out_file),"</table>\n</body>\n</html>\n");
        /* close the output file, so we dont sit on it and block it */
        if (out_file && output_method==HTML_OUT && daemonize) { fclose(out_file); out_file=NULL; }
#endif
        /* handle the number of max outputs if set */
		if ((
#ifdef CSV					
			    output_method==CSV_OUT || 
#endif					
				output_method==PLAIN_OUT) && output_count>0) { 
			output_count--;
            /* go to exit if we are done, will break the while(1) */
			if (output_count==0) break;
		}
        /* either refresh the output and handle gui input */
#ifdef HAVE_CURSES		
		if (output_method==CURSES_OUT || output_method==CURSES2_OUT) {
			refresh();
			handle_gui_input(getch());
		} else 
#endif			
        /* or just wait delay ms */
			usleep(delay*1000);
        
        /* quit if we should only output once */
		if (output_method==PLAIN_OUT_ONCE 
#ifdef HTML				
				|| (output_method==HTML_OUT && !daemonize)
#endif
				) break; /* dont loop when we have plain output */
    }
#ifdef HTML	
	/* do we need to output for html? */
	if (output_method==HTML_OUT && !daemonize) {
		print_header(0);
		get_iface_stats(1);
        if (html_header) fprintf(out_file==NULL ? stdout : out_file,"</table>\n</body>\n</html>\n");
	}
#endif	
	/* do we need to output for plain? */
	if (output_method==PLAIN_OUT_ONCE) {
		print_header(0);
		get_iface_stats(1);
	}
	deinit(NULL);
	return 0; /* only to avoid gcc warning */
}
