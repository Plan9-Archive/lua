#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include "lp9lib.h"

/* macro to `unsign' a character */
#define uchar(c)        ((unsigned char)(c))

static int pushresult (lua_State *L, int i, const char *msg);
static int notefun(void *a, char *msg) ;
int killflag = 0;

static int pusherr(lua_State *L){
    char buf[80];
    errstr(buf,sizeof(buf));
    return pushresult(L,0,buf);
}

static int getintfield (lua_State *L, const char *key,int res) {
  lua_getfield(L, -1, key);
  if (lua_isnumber(L, -1))
    res = (int)lua_tointeger(L, -1);
  lua_pop(L, 1);
  return res;
}

static char *getstrfield (lua_State *L, const char *key) {
  char *res = nil;
  lua_getfield(L, -1, key);
  if (lua_isstring(L, -1))
    res = lua_tostring(L, -1);
  lua_pop(L, 1);
  return res;
}

static void setintfield (lua_State *L, const char *key, long value) {
  lua_pushinteger(L, value);
  lua_setfield(L, -2, key);
}

static void setstrfield (lua_State *L, const char *key, char *value) {
  lua_pushstring(L, value);
  lua_setfield(L, -2, key);
}

static int p9_access(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  const ulong mode = luaL_checkinteger(L, 2);
  int r = access(name,mode); // r is 0 or -1, 0: true, -1:false
  lua_pushboolean(L, !r); // 1: true, 0:false
  return 1;
}

/*	look /sys/src/ape/lib/ap/plan9	*/
static int p9_open(lua_State *L){
  const int na = lua_gettop(L);    /* number of arguments */
  const char *name = luaL_checkstring(L, 1);
  const char *mode = "r";
  int omode = OREAD;
  if (lua_isnoneornil(L, 2))  /* called without args? */
    na = 1;
  if(na > 1)
    mode = luaL_checkstring(L, 2);
  int fd = -2;
  if(strcmp(mode,"r") == 0)
    fd = open(name,O_RDONLY);
  else if(strcmp(mode,"w") == 0)
    fd = open(name,O_WRONLY);
  else if(strcmp(mode,"rw") == 0)
    fd = open(name,O_RDWR);
  else if(strcmp(mode,"w+") == 0)
    fd = open(name, O_WRONLY|O_CREAT|O_TRUNC, 0666L);
  else if(strcmp(mode,"a") == 0){
    fd = open(name,O_WRONLY|O_APPEND);
    //seek(fd,0,2);
  }
  if(fd == -2){
    lua_pushstring(L,"invalid mode");
    lua_error(L);
  }
  if(fd < 0)
    return pusherr(L);
  lua_pushinteger(L, fd);
  return 1;
}

static int p9_close(lua_State *L){
  const int fd = luaL_checkinteger(L, 1);
  close(fd);
  lua_pushnil(L);
  return 1;
}

/*	stolen from liolib.c	*/
static int pushresult (lua_State *L, int i, const char *msg) {
  int en = errno;  /* calls to Lua API may change this value */
  if (i) {
    lua_pushboolean(L, 1);
    return 1;
  }
  else {
    lua_pushnil(L);
    if (msg)
      lua_pushfstring(L, "%s: %s", msg, strerror(en));
    else
      lua_pushfstring(L, "%s", strerror(en));
    lua_pushinteger(L, en);
    return 3;
  }
}


static int p9_read(lua_State *L){
  const int na = lua_gettop(L);    /* number of arguments */
  const int fd = luaL_checkinteger(L, 1);
  char *type = "string";
  long n;
  char *status = nil;
  char buf[16384];
  if(na == 1){
	luaL_Buffer lb;
 	luaL_buffinit(L, &lb);
    while((n = read(fd,buf,sizeof(buf))) > 0)
      luaL_addlstring(&lb,buf,n);
    if(n < 0)
      return pusherr(L);
    luaL_pushresult(&lb);
  }
  else{
    ulong m;
    char *p = buf;
    m = luaL_checkinteger(L, 2);
    if(m > sizeof(buf))
      p = malloc(m);
    if(p == nil){
      lua_pushstring(L,"malloc");
      lua_error(L);
    }
    n = read(fd,p,m);
    if(n < 0){
      if(p != buf)
        free(p);
      return pusherr(L);
    }
    if(n == 0)
      lua_pushnil(L);
    else
      lua_pushlstring(L,p,n);
    if(p != buf)
      free(p);
  }
  return 1;
}

static int p9_write(lua_State *L){
  const int fd = luaL_checkinteger(L, 1);
  size_t size;
  const char *data = luaL_checklstring(L, 2, &size);
  long n;
  n = write(fd,data,size);
  if(n < 0)
    return pusherr(L);
    /* you might think this case should raise error
     * however we have cases that we must neglect the return value
     * see announce.c	*/
  lua_pushinteger(L, n);
  return 1;
}

static int p9_seek(lua_State *L){
  const int na = lua_gettop(L);    /* number of arguments */
  const int fd = luaL_checkinteger(L, 1);
  vlong off = 0;
  int how = 0;
  char *_how = nil;
  if(na > 1)
    off = luaL_checkinteger(L, 2);
  if(na > 2){
    _how = luaL_checkstring(L, 3);
    if(strcmp(_how,"set") == 0)
      how = 0;
    else if(strcmp(_how,"cur") == 0)
      how = 1;
    else if(strcmp(_how,"end") == 0)
      how = 2;
    else{
      lua_pushstring(L,"must be one of \"set\",\"cur\",\"end\"");
      lua_error(L);
    }
  }
  off = seek(fd,off,how);
  if(off < 0){
    lua_pushstring(L,"seek");
    lua_error(L);
  }
  else
    lua_pushinteger(L, (int)off);
  return 1;
}


static int p9_bind(lua_State *L){
  const int na = lua_gettop(L);    /* number of arguments */
  const char *name = luaL_checkstring(L, 1);
  const char *old = luaL_checkstring(L, 2);
  char *mode = "";
  ulong flag = 0;
  if(na > 2)
    mode = luaL_checkstring(L, 3);
  if(strchr(mode,'a'))
    flag |= MAFTER;
  if(strchr(mode,'b'))
    flag |= MBEFORE;
  if(strchr(mode,'c'))
    flag |= MCREATE;

  if((flag&MAFTER)&&(flag&MBEFORE)){
    lua_pushstring(L,"flags:");
    lua_error(L);
  }
  if(bind(name,old,flag) < 0)
    lua_pushboolean(L,0);
  else
    lua_pushboolean(L,1);
  return 1;
}

static int p9_unmount(lua_State *L){
  const int na = lua_gettop(L);    /* number of arguments */
  const char *name = luaL_checkstring(L, 1);
  const char *old = nil;
  if(na > 1)
    old = luaL_checkstring(L, 2);
  if(old == nil){
    old = name;
    name = nil;
  }
  if(unmount(name,old) < 0)
    lua_pushboolean(L,0);
  else
    lua_pushboolean(L,1);
  return 1;
}

static int pushdir(lua_State *L, Dir *dir){
    lua_createtable(L, 0, 12);  /* 12 = number of fields */
    char buf[64];
    setintfield(L, "type", dir->type);
    setintfield(L, "dev", dir->dev);
    snprint(buf,sizeof(buf),
      "%ullx-%uld-%d",dir->qid.path,dir->qid.vers,dir->qid.type);
    setstrfield(L, "qid", buf);
    /* lua is buggy in handling integer such as 0x80000000
	 * thereby we use string "mode" */
    snprint(buf,sizeof(buf),"0%uo",dir->mode);
    setstrfield(L, "mode", buf);
    setintfield(L, "atime", dir->atime);
    setintfield(L, "mtime", dir->mtime);
    setintfield(L, "length", dir->length);
    setstrfield(L, "name", dir->name);
    setstrfield(L, "uid", dir->uid);
    setstrfield(L, "gid", dir->gid);
    setstrfield(L, "muid", dir->muid);
    return 0;
}

static int p9_dirstat(lua_State *L){
  const char *name = luaL_checkstring(L, 1);
  Dir *dir;
  /* look os.date() for helpful reference */
  dir = _dirstat(name);
  if(dir == nil) /* non-existent */
    lua_pushnil(L);
  else{
    pushdir(L,dir);
    free(dir);
  }
  return 1;
}

/* 	dirwstat(file,mode) where mode is a table such as {uid="alice"}
 * 	we have a good example: os.time() */
static int p9_dirwstat(lua_State *L){
  const char *name = luaL_checkstring(L, 1);
  const mask = 0x80000000;
  ulong mode = 0;
  ulong mtime;
  char *uid,*gid,*smode;
  Dir *dir;

  if (lua_isnoneornil(L, 1))  /* called without args? */
    return luaL_error(L, LUA_QL("setmode") " need a table");
  luaL_checktype(L, 2, LUA_TTABLE);
  lua_settop(L, 2);  /* make sure table is at the top */
  /* mode in dirstat is string octal, so we must ... */
  smode = getstrfield(L,"mode");
  if(smode)
    mode = strtol(smode,nil,8);
  
  mtime = getintfield(L,"mtime",0);
  uid = getstrfield(L,"uid");
  gid = getstrfield(L,"gid");

  /* debug */
  //fprintf(stderr,"mode=0%o mtime=%lu uid=%s gui=%s\n",mode,mtime,uid,gid);

  /* we should keep dir bit of mode for safty */
  dir = _dirstat(name);
  if(dir == nil) /* non-existent */
    lua_pushnil(L);
  else{
    if(mode)
      dir->mode = ((dir->mode)&mask)?(mode|mask):(mode&(~mask));
    //fprintf(stderr,"mode=0%o\n",dir->mode); 
    if(mtime) dir->mtime = mtime;
    if(uid) dir->uid = uid;
    if(gid) dir->gid = gid;
    _dirwstat(name, dir);
  }
  free(dir);
  return 1;
}

static int p9_mkdir(lua_State *L){
  /* return nil in success */
  ulong mode =  0777L;
  int fd;
  char buf[128];
  const int na = lua_gettop(L);    /* number of arguments */
  const char *name = luaL_checkstring(L, 1);
  const char *m;
  if(na > 1){
    m = luaL_checkstring(L, 2);
    mode = strtoul(m, &m, 8);
    if(mode > 0777){
      snprint(buf,sizeof(buf),"mkdir: inappropriate mode.");
      lua_pushstring(L,buf);
	  lua_error(L);
      return 1;
    }
  }
  fd = create(name,OREAD, DMDIR | mode);
  if(fd < 0){
    snprint(buf,sizeof(buf),"create: %r");
    lua_pushstring(L,buf);
	lua_error(L);
  }else{
    lua_pushboolean(L, 1);
	close(fd);
  }
  return 1;
}

static int compar(Dir *a, Dir *b){
	return strcmp(a->name, b->name);
}

static int p9_readdir(lua_State *L){
  const int m = lua_gettop(L);    /* number of arguments */
  const char *name = luaL_checkstring(L, 1);
  const int t = 0;
  Dir *dir;
  long n;
  int fd;
  int i,r;

  if(m > 1) t = lua_toboolean(L,2);
  fd = open(name,O_RDONLY);
  if(fd < 0){/* non-existent */
    lua_pushnil(L);
    return 1;
  }

  if((n = _dirreadall(fd, &dir)) < 0){
    lua_pushnil(L);
    close(fd);
    return 1;
  }
  /* sort by name */
  qsort(dir, n, sizeof(Dir), (int (*)(void*, void*))compar);

  lua_createtable(L, 0, 1);  /* n = size of array */

  if(t){
    for(i = 0; i < n; i++){
      pushdir(L, &dir[i]);
      lua_setfield(L,-2,dir[i].name);
    }
  }
  else{
    for(i = 0; i < n; i++){
      r = ((dir[i].mode&DMDIR) != (ulong)0);
      lua_pushboolean(L, r);
      lua_setfield(L,-2,dir[i].name);
    }
  }
  close(fd);
  return 1;// number of pop
}


/*	usage: bit(n,"&", m)	 */
static int p9_bit(lua_State *L) {
	ulong n = luaL_checkinteger(L, 1);
	const char *s = luaL_checkstring(L, 2);
	const ulong m = luaL_checkinteger(L, 3);
	if(strcmp(s,"&") == 0) n &= m;
	else if(strcmp(s,"|") == 0) n |= m;
	else if(strcmp(s,"^") == 0) n = n ^ m;
	else if(strcmp(s,"<<") == 0) n = n << m;
	else if(strcmp(s,">>") == 0) n = n >> m;
	else {
		lua_pushstring(L,"invalid operator");
		lua_error(L);
	}
	lua_pushinteger(L, n);
	return 1;
}

/*	usage: popen(prog) 
	prog is rc command list	*/
static int p9_popen(lua_State *L) {
	char *prog = luaL_checkstring(L, 1);
	int fd[2];
	if(pipe(fd)<0){
		lua_pushstring(L,"pipe");
		lua_error(L);
	}
	switch(rfork(RFFDG|RFPROC|RFNOTEG|RFREND|RFNAMEG)){
	/*	RFNAMEG: the new process inherits a copy of the parent's name space.
	*	RFNAMEG is required so that nomnt() does not make effect to parent
	*	namespace	*/
	case -1:
		lua_pushstring(L,"rfork");
		lua_error(L);
	case 0: // child: read stdin, and write to stdout
		close(fd[0]);
		dup2(fd[1], 0); // the stdin is redirected to fd
		dup2(fd[1], 1); // the stdout is redirected to fd
		//dup2(fd[1], 2); // the stderr is also redirected to fd
		close(fd[1]);
		execl("/bin/rc", "rc", "-c", prog, nil);
		/* not reached in normal case*/
		lua_pushstring(L,"exec");
		lua_error(L);
	default:// parent: read data from the child
		close(fd[1]);
		break;
	}
	lua_pushinteger(L, fd[0]);
	return 1;
}


static int p9_fork(lua_State *L) {
	int pid;
	pid = fork();
	if(pid < 0){
		lua_pushstring(L,"fork");
		lua_error(L);
	}
	lua_pushinteger(L, pid);
	return 1;
}

static int p9_wait(lua_State *L){
	Waitmsg *msg;
	int na = lua_gettop(L);    /* number of arguments */
	int pid = 0;
	if(na > 0)
		pid = luaL_checkinteger(L, 1);
	//msg = wait(); // ape wait
	msg = _WAIT(); // plan9 wait
    if(msg == nil)
		goto noproc;
	if(pid != 0){
		while(msg->pid != pid){
			free(msg);
			//msg = wait();
			msg = _WAIT();
		    if(msg == nil)
				goto noproc;
		}
	}
	lua_pushinteger(L, pid);
	if(msg->msg[0] == 0)
		lua_pushnil(L);
	else
		lua_pushstring(L,msg->msg);
	lua_pushinteger(L, msg->time[0]);
	lua_pushinteger(L, msg->time[1]);
	lua_pushinteger(L, msg->time[2]);
	free(msg);
	return 5;
noproc:
	lua_pushnil(L);
	lua_pushstring(L,"no process");
	return 2;
}

static int p9_alarm(lua_State *L) {
	int na = lua_gettop(L);    /* number of arguments */
	long timeout = luaL_checkinteger(L, 1);
	if(na > 1)
		killflag = lua_toboolean(L,2);
	if(timeout > 0){
		atnotify(notefun,1);
		timeout = alarm(timeout);
	}
	else{
		killflag = 0;
		timeout = alarm(0);
		atnotify(notefun,0);
	}
	lua_pushinteger(L, timeout);
	return 1;
}

static int notefun(void *a, char *msg) {
	USED(a);
	if(killflag == 0 && (strcmp(msg, "alarm") == 0)){
		return 1;
	}
	return 0;
}

static int p9_sleep(lua_State *L) {
	long time = luaL_checkinteger(L, 1);
	sleep(time);
	lua_pushinteger(L, time);
	return 1;
}

static const luaL_Reg p9lib[] = {
  {"open",p9_open},
  {"close",p9_close},
  {"read",p9_read},
  {"write",p9_write},
  {"seek",p9_seek},
  {"bind",p9_bind},
  {"unmount",p9_unmount},
  {"access",p9_access},
  {"dirstat",p9_dirstat},
  {"dirwstat",p9_dirwstat},
  {"readdir",p9_readdir},
  {"mkdir",p9_mkdir},
  {"popen",p9_popen},
  {"fork",p9_fork},
  {"wait",p9_wait},
  {"alarm",p9_alarm},
  {"sleep",p9_sleep},
  {"bit",p9_bit},
  {NULL, NULL}
};

/*
** Open string library
*/
LUALIB_API int lua_p9lib (lua_State *L) {
  luaL_newlib(L, p9lib);
  return 1;
}

