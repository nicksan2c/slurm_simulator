#ifdef SLURM_SIMULATOR

#include <stdio.h>
#include <stdlib.h>
#include <mysql.h>
#include <fcntl.h>
#include <string.h>
#include "sim_trace.h"
#include <ctype.h>
#include <unistd.h>
#include <getopt.h>
#include <pwd.h>

#define DEFAULT_START_TIMESTAMP 1420066800
static const char DEFAULT_START[] = "2015-01-01 00:00:00";

/* Flag set by ‘--verbose’. */
static int verbose_flag;

void finish_with_error(MYSQL *con)
{
	fprintf(stderr, "%s\n", mysql_error(con));
	mysql_close(con);
	exit(1);
}

void print_usage()
{
	printf("\
Usage: mysql_trace_builder [OPTIONS]\n\
    -s, --starttime time            Start selecting jobs from this time\n\
                                    format: \"yyyy-MM-DD hh:mm:ss\"\n\
    -e, --endtime   time            Stop selecting jobs at this time\n\
                                    format: \"yyyy-MM-DD hh:mm:ss\"\n\
    -h, --host      db_hostname     Name of machine hosting MySQL DB\n\
    -u, --user      dbuser          Name of user with which to establish a\n\
                                    connection to the DB\n\
    -t, --table     db_table        Name of the MySQL table to query\n\
    -v, --verbose                   Increase verbosity of the messages\n\
    -f, --file      filename        Name of the output trace file being created\n\
    -p, --help                      This help message\n\n\
");
}

int 
main(int argc, char **argv)
{

	int i,c,written,j;
	int trace_file;
	char year[4], month[2], day[2], hours[2], minutes[2], seconds[2];
	
	char *endtime = NULL;
	char *file = "test.trace";
	char *host = NULL; 
	char *starttime = "2015-01-01 00:00:00";
	char *table = NULL;
	char *user = NULL;
	char password[20];

	MYSQL *conn;
	MYSQL_RES *res;
	MYSQL_ROW row;
	char query[300];
	job_trace_t new_trace;


	while(1) {
		static struct option long_options[] = {
			/* These options set a flag. */
			{"verbose", no_argument,       &verbose_flag, 1},
			/* These options don’t set a flag.
			We distinguish them by their indices. */
			{"endtime",   required_argument, 0, 'e'},
			{"file",      required_argument, 0, 'f'},
			{"host",      required_argument, 0, 'h'},
			{"help",      no_argument,	 0, 'p'},
			{"starttime", required_argument, 0, 's'},
			{"table",     required_argument, 0, 't'},
			{"user",      required_argument, 0, 'u'},
			{0, 0, 0, 0}
		};

		/* getopt_long stores the option index here. */
		int option_index = 0;
		c = getopt_long (argc, argv, "ve:f:h:ps:t:u:",long_options,
						&option_index);

		/* Detect the end of the options. */
		if (c == -1) break;

		switch (c) {
		case 0:
			/* If this option set a flag, do nothing else now. */
			if (long_options[option_index].flag != 0) break;
			printf ("option %s", long_options[option_index].name);
			if (optarg)
				printf (" with arg %s", optarg);
			printf ("\n");
			break;

		case 'v':
			verbose_flag = 1;
			break;
		case 'e':
			endtime = optarg;
			break;

		case 'f':
			file = optarg;
			break;

		case 'h':
			host = optarg;
			break;

		case 'p':
			print_usage();
			exit(0);

		case 's':
			starttime = optarg;
			break;

		case 't':
			table = optarg;
			break;

		case 'u':
			user = optarg;
		        break;

		case '?':
			break;

		default:
			print_usage();
			abort ();
       		}


	} /* while */

	if (verbose_flag)
		printf("Selected options:\n\nstart time \t\t%s\nend time \t\t%s"
			"\nfile out \t\t%s\ntable \t\t\t%s\n",
			starttime, endtime, file, table);

	/* Print any remaining command line arguments (not options). */
	if (optind < argc) {
		printf ("non-option ARGV-elements: ");
		while (optind < argc)
		        printf ("%s ", argv[optind++]);
		putchar ('\n');
	}


	/*validate input parameter*/
	if ((endtime == NULL) || (user == NULL) || (table == NULL) ||
						(host == NULL)) {
		printf("endtime, user, table and host cannot be NULL!\n");
		print_usage();
		exit(-1);
	}

	if ( sscanf(endtime, "%4s-%2s-%2s %2s:%2s:%2s", year, month, day, hours,
						minutes, seconds) != 6 ) {
		printf("ERROR validate endtime\n");
		print_usage();
		return -1;
	}


	if ( sscanf(starttime, "%4s-%2s-%2s %2s:%2s:%2s", year, month, day,
					 hours, minutes, seconds) != 6 ) {
		printf("ERROR validate starttime\n");
		print_usage();
		return -1;
	}	
	

	/*read the password*/
	strncpy(password, getpass("Type your DB Password: "), 20);
	/* printf("You entered pass %s\n", password); */
	
	/* mysql stuff */
	conn = mysql_init(NULL);
	snprintf(query, sizeof(query), "SELECT id_job, account, cpus_req, "
		"id_user, partition, time_submit, timelimit, (time_end-"
		"time_start) as duration, cpus_alloc, nodes_alloc from %s "
		"where FROM_UNIXTIME(time_submit) BETWEEN '%s' AND '%s' AND "
		"time_end>0 AND nodes_alloc>0", table, starttime, endtime);
	printf("\nQuery --> %s\n\n", query);

	/* Connect to database */
	if (!mysql_real_connect(conn, host, user, password, "slurm_acct_db",
							0, NULL, 0)) {
		finish_with_error(conn);
	}

	/* send SQL query */
	if (mysql_query(conn, query)) {
		finish_with_error(conn);
	}
	res = mysql_store_result(conn);

	if (res == NULL) {
		finish_with_error(conn);
	}

	int num_fields = mysql_num_fields(res);

	/* writing results to file */
	if((trace_file = open(file, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR |
						S_IRGRP | S_IROTH)) < 0) {
		printf("Error opening file %s\n", file);
		return -1;
	}


	j = 0;
	while ((row = mysql_fetch_row(res))) {

		for( i = 0; i < num_fields; i++) {
			printf("%s ", row[i] ? row[i] : "NULL");
		}
		printf("\n");
		j++;

		new_trace.job_id = atoi(row[0]);
		new_trace.submit = strtoul(row[5], NULL, 0);
		sprintf(new_trace.username, "%s", row[3]);
		sprintf(new_trace.partition, "%s", row[4]);
		sprintf(new_trace.account, "%s", row[1]);
		new_trace.duration = atoi(row[7]);
		new_trace.wclimit = atoi(row[6]);


		new_trace.cpus_per_task = 1;
		new_trace.tasks_per_node = atoi(row[8])/atoi(row[9]);
		new_trace.tasks = atoi(row[8]);

		sprintf(new_trace.reservation, "%s", "");
		sprintf(new_trace.qosname, "%s", "");

		written = write(trace_file, &new_trace, sizeof(new_trace));
		if(written != sizeof(new_trace)) {
			printf("Error writing to file: %d of %ld\n", written,
							sizeof(new_trace));
			return -1;
		}

	}

	printf("\nSuccessfully written file %s : Total number of jobs = %d\n",
								file, j);

	/* close connection */
	mysql_free_result(res);
	mysql_close(conn);

	exit(0);
}
#else
#include <stdio.h>
int
main(int argc, char* argv[])
{
	printf("%s placeholder.  Only does something if built in simulator mode"
	       " (LIBS=-lrt CFLAGS=\"-D SLURM_SIMULATOR\").\n", argv[0]);
	return 0;
}
#endif
