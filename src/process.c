/******************************************************************************
 *  bwm-ng parsing and retrieve stuff                                         *
 *                                                                            *
 *  Copyright (C) 2004 Volker Gropp (vgropp@pefra.de)                         *
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

#include "process.h"


/* returns the whether to show the iface or not
 * if is in list return 1, if list is prefaced with ! or 
 * name not found return 0 */
short show_iface(char *instr, char *searchstr) {
	int pos = 0,k,i=0,success_ret=1;
    if (instr==NULL) return success_ret;
    if (instr[0]=='%') {
        success_ret=!success_ret;
        i++;
    }
	k = strlen( searchstr );
	for (;i<=strlen(instr);i++) {
		switch ( instr[i] ) {
			case 0:
			case ',':
				if ( k == pos && ! strncasecmp( &instr[i] - pos, searchstr, pos ) ) {
					return success_ret;
                }
				pos = 0;
				break;
			default:
				pos++;
				break;
		}
    }
	return !success_ret;
}


/* counts the tokens in a string */
long count_tokens(char *in_str) {
    long tokens=0;
    long i=0;
    char in_a_token=0;
    char *str;

    if (in_str==NULL) return 0;
    str=strdup(in_str);
    while (str[i]!='\0') {
        if (str[i]>32) {
            if (!in_a_token) {
                tokens++;
                in_a_token=1;
            }
        } else {
            if (in_a_token) in_a_token=0;
        }
        i++;
    }
    free(str);
    return tokens;
}


#if HAVE_GETTIMEOFDAY
/* Returns: the time difference in milliseconds. */
inline long tvdiff(struct timeval newer, struct timeval older) {
  return labs((newer.tv_sec-older.tv_sec)*1000+
    (newer.tv_usec-older.tv_usec)/1000);
}

/* returns the milliseconds since old stat */
float get_time_delay(int iface_num) {
    struct timeval now;
    float ret;
    gettimeofday(&now,NULL);
    ret=(float)1000/tvdiff(now,if_stats[iface_num].time);
    if_stats[iface_num].time.tv_sec=now.tv_sec;
    if_stats[iface_num].time.tv_usec=now.tv_usec;    
    return ret;
}
#endif

inline unsigned long long calc_new_values(unsigned long long new, unsigned long long old) {
    /* FIXME: WRAP_AROUND _might_ be wrong for libstatgrab, where the type is always long long */
    return (new>=old) ? (unsigned long long)(new-old) : (unsigned long long)((
#ifdef HAVE_LIBKSTAT
            (input_method==KSTAT_IN) ?
            WRAP_32BIT :
#endif
            WRAP_AROUND)
            -old)+new;
}


/* calc actual new-old values */
t_iface_speed_stats convert2calced_values(t_iface_speed_stats new, t_iface_speed_stats old) {
    t_iface_speed_stats calced_stats;
    calced_stats.errors.in=calc_new_values(new.errors.in,old.errors.in);
    calced_stats.errors.out=calc_new_values(new.errors.out,old.errors.out);
    calced_stats.packets.out=calc_new_values(new.packets.out,old.packets.out);
    calced_stats.packets.in=calc_new_values(new.packets.in,old.packets.in);
    calced_stats.bytes.out=calc_new_values(new.bytes.out,old.bytes.out);
    calced_stats.bytes.in=calc_new_values(new.bytes.in,old.bytes.in);
    return calced_stats;
}


inline void save_sum(struct inout_long *stats,struct inout_long new_stats_values,struct inout_long old_stats_values) {
    stats->in+=calc_new_values(new_stats_values.in,old_stats_values.in);
    stats->out+=calc_new_values(new_stats_values.out,old_stats_values.out);
}

    
inline void save_max(struct inouttotal_double *stats,struct inout_long calced_stats,float multiplier) {
    if (multiplier*calced_stats.in > stats->in)
        stats->in=multiplier*calced_stats.in;
    if (multiplier*calced_stats.out>stats->out)
        stats->out=multiplier*calced_stats.out;
    if (multiplier*(calced_stats.out+calced_stats.in)>stats->total)
        stats->total=multiplier*(calced_stats.in+calced_stats.out);
}

inline void init_double_types(struct inouttotal_double *in) {
    in->out=in->in=in->total=0;    
}

int process_if_data (int hidden_if, t_iface_speed_stats tmp_if_stats,t_iface_speed_stats *stats, char *name, int iface_number, char verbose, char iface_is_up) {
#if HAVE_GETTIMEOFDAY
    float multiplier;
#else
	float multiplier=(float)1000/delay;
#endif    
	int local_if_count;
    t_iface_speed_stats calced_stats;
    
    /* if_count starts at 1 for 1 interface, local_if_count starts at 0 */
    for (local_if_count=0;local_if_count<if_count;local_if_count++) {
        /* check if its the correct if */
        if (!strcmp(name,if_stats[local_if_count].if_name)) break;
    }
    if (local_if_count==if_count) {
        /* iface not found, seems like there is a new one! */
        if_count++;
        /* alloc and init */
        if_stats=(t_iface_stats*)realloc(if_stats,sizeof(t_iface_stats)*if_count);
        memset(&if_stats[local_if_count],0,(size_t)sizeof(t_iface_stats));
        /* copy the iface name or add a dummy one */
        if (name[0]!='\0')
            if_stats[if_count-1].if_name=(char*)strdup(name);
        else
            if_stats[if_count-1].if_name=(char*)strdup("unknown");
        /* set it to current value, so there is no peak at first announce */
        if_stats[local_if_count].data=tmp_if_stats;
        if (sumhidden || ((show_all_if>1 || iface_is_up) &&
            (show_all_if || show_iface(iface_list,name)))) {
            /* add the values to total stats now */
            if_stats_total.data.bytes.out+=tmp_if_stats.bytes.out;
            if_stats_total.data.bytes.in+=tmp_if_stats.bytes.in;
            if_stats_total.data.packets.out+=tmp_if_stats.packets.out;
            if_stats_total.data.packets.in+=tmp_if_stats.packets.in;
            if_stats_total.data.errors.out+=tmp_if_stats.errors.out;
            if_stats_total.data.errors.in+=tmp_if_stats.errors.in;
        }
    }
#if HAVE_GETTIMEOFDAY
    multiplier=(float)get_time_delay(local_if_count);
#endif
    /* calc new-old, so we have the new bytes,errors,packets */
    calced_stats=convert2calced_values(tmp_if_stats,if_stats[local_if_count].data);
    /* save new max values in both, calced (for output) and ifstats */
    save_max(&if_stats[local_if_count].max.bytes,calced_stats.bytes,multiplier);
    save_max(&if_stats[local_if_count].max.errors,calced_stats.errors,multiplier);
    save_max(&if_stats[local_if_count].max.packets,calced_stats.packets,multiplier);
    save_sum(&if_stats[local_if_count].sum.bytes,tmp_if_stats.bytes,if_stats[local_if_count].data.bytes);
    save_sum(&if_stats[local_if_count].sum.packets,tmp_if_stats.packets,if_stats[local_if_count].data.packets);
    save_sum(&if_stats[local_if_count].sum.errors,tmp_if_stats.errors,if_stats[local_if_count].data.errors);
    if (verbose) { /* any output at all? */
        /* cycle: show all interfaces, only those which are up, only up and not hidden */
        if ((show_all_if>1 || iface_is_up) && /* is it up or do we show all ifaces? */
            (show_all_if || show_iface(iface_list,name))) {
            print_values(5+iface_number-hidden_if,2,name,calced_stats,multiplier,if_stats[local_if_count]);
		} else
            hidden_if++; /* increase the opt cause we dont show this if */
    }
    /* save current stats for the next run */
    if_stats[local_if_count].data=tmp_if_stats;
    /* add stats to new total */
    if (sumhidden || ((show_all_if>1 || iface_is_up) &&
            (show_all_if || show_iface(iface_list,name)))) {
        stats->bytes.out+=tmp_if_stats.bytes.out;
        stats->bytes.in+=tmp_if_stats.bytes.in;
        stats->packets.out+=tmp_if_stats.packets.out;
        stats->packets.in+=tmp_if_stats.packets.in;
        stats->errors.out+=tmp_if_stats.errors.out;
        stats->errors.in+=tmp_if_stats.errors.in;
    } 
	return hidden_if;
}	

void finish_iface_stats (char verbose, t_iface_speed_stats stats, int hidden_if, int iface_number) {
    int i;
    t_iface_speed_stats calced_stats;
#if HAVE_GETTIMEOFDAY
    struct timeval now;
    float multiplier;
    gettimeofday(&now,NULL);
    multiplier=(float)1000/tvdiff(now,if_stats_total.time);
    if_stats_total.time.tv_sec=now.tv_sec;
    if_stats_total.time.tv_usec=now.tv_usec;
#else
	float multiplier=(float)1000/delay;
#endif    
    calced_stats=convert2calced_values(stats,if_stats_total.data);
    /* save new max values in both, calced (for output) and final stats */
    save_max(&if_stats_total.max.bytes,calced_stats.bytes,multiplier);
    save_max(&if_stats_total.max.errors,calced_stats.errors,multiplier);
    save_max(&if_stats_total.max.packets,calced_stats.packets,multiplier);
    save_sum(&if_stats_total.sum.bytes,stats.bytes,if_stats_total.data.bytes);
    save_sum(&if_stats_total.sum.packets,stats.packets,if_stats_total.data.packets);
    save_sum(&if_stats_total.sum.errors,stats.errors,if_stats_total.data.errors);

    if (verbose) {
        /* output total ifaces stats */
#ifdef HAVE_CURSES		
        if (output_method==CURSES_OUT)
            mvwprintw(stdscr,5+iface_number-hidden_if,2,"---------------------------------------------------------------------------");
        else 
#endif			
			if (output_method==PLAIN_OUT || output_method==PLAIN_OUT_ONCE)
				printf("%s---------------------------------------------------------------------------\n",output_method==PLAIN_OUT ? " " : "");
        print_values(6+iface_number-hidden_if,2,"total",calced_stats,multiplier,if_stats_total);
    }
    /* save the data in total-struct */
    if_stats_total.data=stats;
	if (output_method==PLAIN_OUT)
		for (i=0;i<if_count-iface_number;i++) printf("%70s\n"," "); /* clear unused lines */
	return;
}
