/*
 * passwd.c - password and group database for the native Win32 port of zsh
 *
 * Windows has no passwd database.  The information is synthesised from the
 * environment: the user name comes from USERNAME (or USER), the home
 * directory from USERPROFILE and the host name from COMPUTERNAME.
 */

#include "win32.h"

#include <pwd.h>
#include <grp.h>

static struct passwd zsh_pw;
static struct group zsh_gr;
static int zsh_pwent_valid;
static char zsh_home[MAX_PATH * 2];
static char zsh_home_env[MAX_PATH * 2];

static const char *
zsh_env(const char *name)
{
    const char *v = getenv(name);

    return (v && *v) ? v : NULL;
}

/**/
static void
zsh_fill_passwd(void)
{
    const char *user = zsh_env("USERNAME");
    const char *domain = zsh_env("USERDOMAIN");
    /* HOME in the environment wins: Windows has no passwd database. */
    const char *home = zsh_env("HOME");
    const char *host = zsh_env("COMPUTERNAME");

    if (!home)
	home = zsh_env("USERPROFILE");
    if (!user)
	user = zsh_env("USER");
    if (!user)
	user = "user";
    if (!domain)
	domain = "WORKGROUP";
    if (!home) {
	const char *drive = zsh_env("HOMEDRIVE");
	const char *dir = zsh_env("HOMEPATH");

	if (drive && dir)
	    snprintf(zsh_home_env, sizeof(zsh_home_env), "%s%s", drive, dir);
	else
	    strcpy(zsh_home_env, "C:/");
	home = zsh_home_env;
    }
    if (!host)
	host = "localhost";

    /*
     * The shell's path handling only understands forward slashes, so
     * the home directory is converted here; Windows accepts both.
     */
    {
	size_t len = strlen(home);
	if (len >= sizeof(zsh_home))
	    len = sizeof(zsh_home) - 1;
	memcpy(zsh_home, home, len);
	zsh_home[len] = '\0';
	zsh_win32_slashes(zsh_home);
    }

    zsh_pw.pw_name = (char *) user;
    zsh_pw.pw_passwd = (char *) "*";
    zsh_pw.pw_uid = 0;
    zsh_pw.pw_gid = 0;
    zsh_pw.pw_gecos = (char *) user;
    zsh_pw.pw_dir = zsh_home;
    zsh_pw.pw_shell = (char *) "cmd.exe";
}

/**/
struct passwd *
getpwuid(uid_t uid)
{
    UNUSED_ARG(uid);
    zsh_fill_passwd();
    return &zsh_pw;
}

/**/
struct passwd *
getpwnam(const char *name)
{
    const char *user = zsh_env("USERNAME");

    if (!user)
	user = zsh_env("USER");
    if (!name || !user || strcmp(name, user) != 0)
	return NULL;
    zsh_fill_passwd();
    return &zsh_pw;
}

/**/
struct passwd *
getpwent(void)
{
    if (zsh_pwent_valid)
	return NULL;
    zsh_pwent_valid = 1;
    zsh_fill_passwd();
    return &zsh_pw;
}

/**/
void
setpwent(void)
{
    zsh_pwent_valid = 0;
}

/**/
void
endpwent(void)
{
    zsh_pwent_valid = 1;
}

/**/
struct group *
getgrgid(gid_t gid)
{
    UNUSED_ARG(gid);
    memset(&zsh_gr, 0, sizeof(zsh_gr));
    zsh_gr.gr_name = (char *) "users";
    zsh_gr.gr_passwd = (char *) "*";
    zsh_gr.gr_gid = 0;
    zsh_gr.gr_mem = NULL;
    return &zsh_gr;
}

/**/
struct group *
getgrnam(const char *name)
{
    if (!name || strcmp(name, "users") != 0)
	return NULL;
    return getgrgid(0);
}
