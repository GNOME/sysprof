/*
 * Plan:
 *
 *       blocking_read()
 *
 *           select (fd);
 *           if (readable)
 *               read();
 *
 *
 *       handle SIGUSR1
 *           write spam to commandline given file
 *       
 */


int
main ()
{
    return 0;
}
