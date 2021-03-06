/*-------------------------------------------------------*/
/* gem.c        ( NTHU CS MapleBBS Ver 3.00 )            */
/*-------------------------------------------------------*/
/* target : 精華區閱讀、編選                             */
/* create : 95/03/29                                     */
/* update : 2000/01/02                                   */
/*-------------------------------------------------------*/

#include "bbs.h"

extern int xo_uquery();
extern int xo_usetup();
extern int cmpchrono();


extern XZ xz[];
extern char xo_pool[];
extern char radix32[];
extern char brd_bits[];

extern BCACHE *bshm;
extern int TagNum;		/* Thor.0724: For tag_char */
extern TagItem TagList[];

/* definitions of Gem Mode */


#define	GM_WORKING	0x01	/* 精華區施工中 */
#define	GM_DELETE	0x02	/* 精華區已刪除 */


#define	GEM_CUT		1
#define	GEM_COPY	2


#ifndef	INADDR_NONE
#define	INADDR_NONE	0xffffffff
#endif


#define	GEM_WAY		3
static int gem_way;

static int GemBufferNum; /* Thor.990414: 提前宣告, 用於gem_head */

static int gem_add();
static int gem_paste();
static int gem_anchor();
static int gem_recycle();

static int
gem_manage(title)
  char* title;
{
  int ch,len;
  char buf[100];
  char *list;
  
  strcpy(buf,title);
 
  len = strlen(cuser.userid); 
  if((list = strrchr(buf,'[')) == NULL)
    return 0;

  ch = *(++list);
  if(ch)
  {
    do
    {
      if (!str_ncmp(list, cuser.userid, len))
      {
        ch = list[len];
        if ((ch == 0) || (ch == '/') || (ch == ']'))
          return 1;
      }
      while (ch = *list++)
      {
        if (ch == '/')
          break;
      }
    } while (ch);
  }

  return 0;
}

static void
gem_item(num, ghdr)
  int num;
  HDR *ghdr;
{
  int xmode, gtype;
  char fpath[64];

  /* ◎☆★◇◆□■▽▼ : A1B7 ... */

  xmode = ghdr->xmode;
  gtype = (char) 0xba;
  if (xmode & GEM_FOLDER)
    gtype += 1;
  if (xmode & GEM_GOPHER)
    gtype += 2;
  prints("%6d%c%c\241%c ", num, (xmode & GEM_RESTRICT) ? ')' : (xmode & GEM_LOCK) ? 'L' :  ' ',
    TagNum && !Tagger(ghdr->chrono, num - 1, TAG_NIN) ? '*' : ' ', gtype);

  /* Thor.0724: 連同 recno 一起比對, 因為在copy,paste後會有chrono一樣的 */
  /* tag_char(ghdr->chrono), gtype); */

  gtype = gem_way;

  if (!(bbstate & STAT_BOARD)&&!HAS_PERM(PERM_ADMIN|PERM_GEM)&&(xmode & GEM_RESTRICT))
    prints("\033[1;33m資料保密！\033[m\n");
  else if(!HAS_PERM(PERM_SYSOP) && (xmode & GEM_LOCK))
    prints("\033[1;33m資料保密！\033[m\n"); 
  else if ((gtype == 0) || (xmode & GEM_GOPHER))
    prints("%-.64s\n", ghdr->title);
  else
  {
    if(xmode & GEM_BOARD)
    {
      sprintf(fpath,"gem/brd/%s/",ghdr->xname);
      prints("%-46.45s%-13s%s\n", ghdr->title,(gtype == 1 ? ghdr->xname : ghdr->owner), access(fpath,R_OK) ? "[deleted]" : ghdr->date);
    }
    else
      prints("%-46.45s%-13s%s\n", ghdr->title,(gtype == 1 ? ghdr->xname : ghdr->owner), ghdr->date);
  }
}


static int
gem_body(xo)
  XO *xo;
{
  HDR *ghdr;
  int num, max, tail;

  max = xo->max;
  if (max <= 0)
  {
    outs("\n\n《精華區》尚在吸取天地間的日精月華 :)");
    if (xo->key >= GEM_LMANAGER)
    {
      max = vans("(A)新增資料 (G)海錨功\能 (W)資源回收筒 [N]無所事事 ");
      switch(max)
      {
        case 'a':
	    max = gem_add(xo);
    	    if (xo->max > 0)
	      return max;
          break;
        case 'g':
            gem_anchor(xo);
          break;
        case 'w':
            gem_recycle(xo);
          break;
      }

    }
    else
    {
      vmsg(NULL);
    }
    return XO_QUIT;
  }

  ghdr = (HDR *) xo_pool;
  num = xo->top;
  tail = num + XO_TALL;
  if (max > tail)
    max = tail;

  move(3, 0);
  do
  {
    gem_item(++num, ghdr++);
  } while (num < max);
  clrtobot();

  return XO_NONE;
}


static int
gem_head(xo)
  XO *xo;
{
  char buf[20];

  vs_head("精華文章", xo->xyz);

  if(xo->key > GEM_USER && GemBufferNum > 0)
  {
    sprintf(buf,"(剪貼版 %d 篇)\n", GemBufferNum);
  }
  else
  {
    buf[0] = '\n'; 
    buf[1] = '\0';
  }

  outs(NECKGEM1);
  outs(buf);
  outs(NECKGEM2);
  return gem_body(xo);
}


static int
gem_toggle(xo)
  XO *xo;
{
  gem_way = (gem_way + 1) % GEM_WAY;
  if(!HAS_PERM(PERM_SYSOP) && (gem_way ==1))
    gem_way = 2;
  return gem_body(xo);
}


static int
gem_init(xo)
  XO *xo;
{
  xo_load(xo, sizeof(HDR));
  return gem_head(xo);
}


static int
gem_load(xo)
  XO *xo;
{
  xo_load(xo, sizeof(HDR));
  return gem_body(xo);
}


/* ----------------------------------------------------- */
/* URL processing routines				 */
/* ----------------------------------------------------- */


#define	URL_MAX_LEN	1024
#define PROXY_HOME      "net/"
#define	PROXY_EXPIRE	(30 * 86400)


static void
url_parse(folder, hdr, site, path)
  char *folder;
  HDR *hdr;
  char *site;
  char *path;
{
  char *str;
  int cc;

  str = hdr->xname;

  /* parse URL's site host */

  for (;;)
  {
    cc = *str++;
    if (cc == '/')
    {
      *site = '\0';
      break;
    }
    *site++ = cc;
  }

  /* parse URL's file path */

  while ((cc = *str++))
  {
    *path++ = cc;
  }

  if (hdr->xmode & GEM_EXTEND)
  {
    char buf[128];

    hdr_fpath(str = buf, folder, hdr);
    cc = readlink(path, str, URL_MAX_LEN);
    path += cc;
  }

  *path = '\0';
}


static int
url_stamp(folder, hdr, utype, host, path, port, chrono)
  char *folder;
  HDR *hdr;
  int utype;
  char *host;
  char *path;
  int port;
  int chrono;
{
  int ch;
  char *head, *tail;

  /* Thor: 保留 title, 原本可能有值 (from gem_title()) */

  memset(hdr, 0, sizeof(HDR) - sizeof(hdr->title));

  head = hdr->xname;
  tail = head + GEM_URLEN;

  while ((ch = *host++))
  {
    if (ch >= 'A' && ch <= 'Z')	/* lower case 'host' */
      ch |= 0x20;
    else if (ch == '.' && !*host)	/* remove trailing . */
      break;
    *head++ = ch;
  }
  *head++ = '/';

  if (!chrono)
    chrono = time(NULL);

  for (;;)
  {
    *head++ = ch = *path++;
    if (!ch)
      break;

    if (head >= tail)		/* extend URL format */
    {
      *head = '\0';
      head = folder = str_dup(folder, 10);
      while ((ch = *head++))
      {
	if (ch == '/')
	  tail = head;
      }

      /* hierarchy */

      if (*tail == '.')
      {
	head = tail++;
	*tail++ = '/';
      }
      else
      {
	head = tail - 2;
      }

      *tail++ = 'X';

      for (;;)
      {
	*head = radix32[chrono & 31];
	archiv32(chrono, tail);
	if (!symlink(path, folder))
	{
	  utype |= GEM_EXTEND;
	  break;
	}

	if (errno != EEXIST)
	  return 0;

	chrono++;
      }

      free(folder);
      break;
    }
  }

  hdr->chrono = chrono;
  hdr->xmode = utype;
  hdr->xid = port;

  return ++chrono;
}


/*-------------------------------------------------------*/
/* GOPHER (URL) routines				 */
/*-------------------------------------------------------*/


#if 0
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>


int
net_open(site, port)
  char *site;
  int port;
{
  struct sockaddr_in sin;
  int sock, cc;

  if ((sin.sin_addr.s_addr = dns_addr(site)) == INADDR_NONE)
    return -1;

  sin.sin_family = AF_INET;
  sin.sin_port = htons(port);
  memset(sin.sin_zero, 0, sizeof(sin.sin_zero));
  sock = socket(AF_INET, SOCK_STREAM, 0);

  if (connect(sock, (struct sockaddr *) & sin, sizeof(sin)))
  {
    close(sock);
    return -1;
  }

  return sock;
}
#endif


static int
url_open(site, path, port)
  char *site;
  char *path;
  int port;
{
  int sock;
  /* Thor.980707: 有時 site會有ip出現, 要在 dns_open內處理or外處理? */
  sock = dns_open(site, port);
  if (sock >= 0)
  {
    port = strlen(path);
    site = path + port;
    *site = '\n';
    port++;
    if (send(sock, path, port, 0) != port)
    {
      close(sock);
      sock = -1;
    }
    *site = '\0';
  }

  return sock;
}


static char *
go_field(str)
  char *str;
{
  int cc;

  for (;;)
  {
    cc = *str;
    if (cc == '\t')
    {
      *str++ = '\0';
      return str;
    }
    if (!cc)
    {
      return NULL;
    }
    str++;
  }
}


int
url_fpath(fpath, folder, hdr)
  char *fpath;
  char *folder;
  HDR *hdr;
{
  int fd, gtype;
  time_t now;
  FILE *fp;
  char *xsite, *xpath, *ptr, *str, site[64], path[URL_MAX_LEN];
  struct stat st;

  /* --------------------------------------------------- */
  /* parse the URL					 */
  /* --------------------------------------------------- */

  url_parse(folder, hdr, xsite = site, xpath = path);
  gtype = (*xpath == '0') ? 'A' : 'F';

  strcpy(fpath, PROXY_HOME);
  str = fpath + sizeof(PROXY_HOME) - 2;

  /* --------------------------------------------------- */
  /* host : make directory hierarchy			 */
  /* --------------------------------------------------- */

  ptr = xsite;
  do
  {
    fd = *ptr++;

    /* remove trailing [.edu.tw] */

    if (fd == '.' && !str_cmp(ptr, "edu.tw"))
      fd = 0;

    *++str = fd;
  } while (fd);

  mak_dirs(fpath);

  /* --------------------------------------------------- */
  /* path : generate (unique ?) filename by hashing	 */
  /* --------------------------------------------------- */

  *str++ = '/';
  folder = str;
  *++str = '/';
  *++str = gtype;
  archiv32(hash32(xpath), ++str);
  *folder = str[6];

  /* --------------------------------------------------- */
  /* check proxy cache first				 */
  /* --------------------------------------------------- */

  now = time(0);
  if (!stat(fpath, &st))
  {
    if (S_ISREG(st.st_mode) && (st.st_mtime > now - PROXY_EXPIRE))
      return 0;
    unlink(fpath);
  }

  /* --------------------------------------------------- */
  /* well, fetch it from network			 */
  /* --------------------------------------------------- */

  outz("★ 建立 proxy 資料連線中 \033[5m...\033[m");
  refresh();

  fd = url_open(xsite, xpath, hdr->xid);
  if (fd < 0)
  {
    zmsg("★ 無法建立連線，請稍後再試，或洽 SYSOP");
    return fd;
  }

  fp = fopen(fpath, "w");
  if (gtype == 'A')
  {
    st.st_mtime = now;
    fprintf(fp, "作者: %s\n標題: %s\n時間: (%s) %s\n",
      xpath, hdr->title, xsite, ctime(&st.st_mtime));
    hdr = NULL;
  }
  else
  {
    hdr = (HDR *) xpath;	/* ie. path[], 借來一用 */
  }

  mgets(-1);

  while ((str = mgets(fd)))
  {
    gtype = *str;
    if (gtype == '.' && str[1] == '\0')
      break;

    if (hdr)
    {
      if (gtype == '0')
	gtype = GEM_GOPHER;
      else if (gtype == '1')
	gtype = GEM_GOPHER | GEM_FOLDER;
      else
	continue;

      if (!(xpath = go_field(++str)))
	continue;

      if (!(xsite = go_field(xpath)))
	continue;

      if (!(ptr = go_field(xsite)))
	continue;

      now = url_stamp(fpath, hdr, gtype, xsite, xpath, atoi(ptr), now);
      if (now <= 0)
	break;

      /* ----------------------------------------------- */
      /* 處理 title 中的特殊字串：◇◆□■ ...		 */
      /* ----------------------------------------------- */

      if (*str == (char) 0xa1 && str[2] == ' ' &&
	(str[1] == (char) 0xba || str[1] == (char) 0xbb ||
	  str[1] == (char) 0xbc || str[1] == (char) 0xbd))
	str += 3;

      str_ncpy(hdr->title, str, TTLEN);
      fwrite(hdr, sizeof(HDR), 1, fp);
    }
    else
    {
      fputs(str, fp);
      fputc('\n', fp);
    }
  }

  close(fd);
  fclose(fp);
  return 0;
}


/* ----------------------------------------------------- */
/* gem_check : attribute / permission check out		 */
/* ----------------------------------------------------- */


#define	GEM_READ	1	/* readable */
#define	GEM_WRITE	2	/* writable */
#define	GEM_FILE	4	/* 預期是檔案 */


static HDR *
gem_check(xo, fpath, op)
  XO *xo;
  char *fpath;
  int op;
{
  HDR *ghdr;
  int gtype, level;
  char *folder;

  level = xo->key;
  if ((op & GEM_WRITE) && (level <= GEM_USER))
    return NULL;

  ghdr = (HDR *) xo_pool + (xo->pos - xo->top);
  gtype = ghdr->xmode;

//  if ((gtype & GEM_RESTRICT) && (level <= GEM_USER))
//    return NULL;

  if ((gtype & GEM_RESTRICT) && (level <= GEM_USER) && !gem_manage(ghdr->title))
    return NULL;
    
  if ((gtype & GEM_LOCK) && (!HAS_PERM(PERM_SYSOP)))
    return NULL;

  if ((op & GEM_FILE) && (gtype & GEM_FOLDER))
    return NULL;

  if (fpath)
  {
    if (gtype & GEM_BOARD)
    {
      sprintf(fpath, "gem/brd/%s/.DIR", ghdr->xname);
    }
    else
    {
      folder = xo->dir;
      if (gtype & GEM_GOPHER)
      {
	if (url_fpath(fpath, folder, ghdr))
	  return NULL;
      }
      else
      {
	hdr_fpath(fpath, folder, ghdr);
      }
    }
  }
  return ghdr;
}


/* ----------------------------------------------------- */
/* 資料之新增：append / insert				 */
/* ----------------------------------------------------- */

/* Thor.981218: 防止惡意作亂 */
static inline int 
site_fake(s)
  char *s;
{
  if (*s=='.') return -1;
  while(*s)
  {
    if(*s == '/')
      return -1;
    s++;
  }
  return 0;
}

static int
url_edit(folder, hdr)
  char *folder;
  HDR *hdr;
{
  char site[64], path[128], port[5];
  int chrono, mode, num;

  if ((chrono = hdr->chrono))
  {
    url_parse(folder, hdr, site, path);
    num = hdr->xid;
    mode = GCARRY;
  }
  else
  {
    num = 70;
    mode = DOECHO;
  }
  sprintf(port, "%d", num);

  if (!vget(b_lines, 0, "Host：", site, sizeof(site), mode))
    return -1;

  /* Thor.981218: 防止惡意作亂 */
  if(site_fake(site))
    return -1;

  switch (vget(b_lines, 0, "Path：", path, sizeof(path), mode))
  {
  case '0':
    mode = GEM_GOPHER;
    break;

  case '1':
    mode = GEM_GOPHER | GEM_FOLDER;
    break;

  default:
    return -1;
  }

  vget(b_lines, 0, "Port：", port, sizeof(port), GCARRY);
  num = atoi(port);
  if (num <= 0 || num >= 10000)
    return -1;

  /* 假設人為指定的 URL 長度不會超過系統預定值 */

#if 0
  if (chrono)
  {
    /* 刪除舊的 symbolic link  ... */
  }
#endif

  url_stamp(folder, hdr, mode, site, path, num, chrono);

  return 0;
}


void
brd2gem(brd, gem)
  BRD *brd;
  HDR *gem;
{
  memset(gem, 0, sizeof(HDR));
  time(&gem->chrono);
  strcpy(gem->xname, brd->brdname);
  sprintf(gem->title, "%-16s%s", brd->brdname, brd->title );
  gem->xmode = GEM_BOARD | GEM_FOLDER;
}


static void
gem_log(folder, action, hdr)
  char *folder;
  char *action;
  HDR *hdr;
{
  char fpath[80], buf[256];

  if (hdr->xmode & (GEM_RESTRICT | GEM_RESERVED | GEM_LOCK))
    return;

  str_folder(fpath, folder, "@/@log");
  sprintf(buf, "[%s] %s (%s) %s\n%s\n\n",
    action, hdr->xname, Now(), cuser.userid, hdr->title);
  f_cat(fpath, buf);
}


static int
gem_add(xo)
  XO *xo;
{
  int gtype, level, fd, ans;
  char title[80], fpath[80], *dir;
  HDR ghdr;

  level = xo->key;
  if (level < GEM_LMANAGER)	/* [回收筒] 中不能新增 */
    return XO_NONE;

  gtype = vans(level == GEM_SYSOP ?
    "新增 A)rticle B)oard C)lass D)ata F)older G)opher P)aste Q)uit [Q] " :
    "新增 (A)文章 (F)卷宗 (G)絲路 (P)貼複 (Q)取消？[Q] ");

  if (gtype == 'p')
    return gem_paste(xo);

  if (gtype != 'a' && gtype != 'f' && gtype != 'g' &&
    (level != GEM_SYSOP || (gtype != 'b' && gtype != 'c' && gtype != 'd')))
    return XO_FOOT;

  dir = xo->dir;
  fd = -1;
  memset(&ghdr, 0, sizeof(HDR));

  if (gtype == 'b')
  {
    BRD *brd;

    if (!(brd = ask_board(fpath, BRD_R_BIT, NULL)))
      return gem_head(xo);

    brd2gem(brd, &ghdr);
    gtype = 0;
  }
  else
  {
    if (!vget(b_lines, 0, "標題：", title, 64, DOECHO))
      return XO_FOOT;

    if (gtype == 'c' || gtype == 'd')
    {
      if (!vget(b_lines, 0, "檔名：", fpath, (gtype == 'c') ? IDLEN :IDLEN + 1, DOECHO))
	return XO_FOOT;

      if (strchr(fpath, '/'))
      {
	zmsg("不合法的檔案名稱");
	return XO_NONE;
      }

      time(&ghdr.chrono);
      sprintf(ghdr.xname, "@%s", fpath);
      if (gtype == 'c')
      {
	strcat(fpath, "/");
	sprintf(ghdr.title, "%-13s【 %s 】", fpath, title);
	ghdr.xmode = GEM_FOLDER;
      }
      else
      {
	strcpy(ghdr.title, title);
	ghdr.xmode = 0;
      }
      gtype = 1;
    }
    else
    {
      if (gtype == 'g')
      {
	gtype = GEM_GOPHER;
	if (url_edit(dir, &ghdr))
	  return XO_FOOT;
      }
      else
      {
	fd = hdr_stamp(dir, gtype, &ghdr, fpath);
	if (fd < 0)
	  return XO_FOOT;
	close(fd);

	if (gtype == 'a')
	{ /* Thor.981020: 注意被talk的問題 */
          if(bbsothermode & OTHERSTAT_EDITING)
          {
            vmsg("你還有檔案還沒編完哦！");
            return XO_FOOT;
          }	
	  else if (vedit(fpath, NA))
	  {
	    unlink(fpath);
	    zmsg(msg_cancel);
	    return gem_head(xo);
	  }
	  gtype = 0;
	}
	else if (gtype == 'f')
	{
	  gtype = GEM_FOLDER;
	}

	ghdr.xmode = gtype;
      }

      strcpy(ghdr.title, title);
    }
  }

  ans = vans("存放位置 A)ppend I)nsert N)ext Q)uit [A] ");

  if (ans == 'q')
  {
    if (fd >= 0)
      unlink(fpath);
    return (gtype ? XO_FOOT : gem_head(xo));
  }

  if (!(gtype & GEM_GOPHER))
    strcpy(ghdr.owner, cuser.userid);

  if (ans == 'i' || ans == 'n')
    rec_ins(dir, &ghdr, sizeof(HDR), xo->pos + (ans == 'n'), 1);
  else
    rec_add(dir, &ghdr, sizeof(HDR));

  gem_log(dir, "新增", &ghdr);

  return (gtype ? gem_load(xo) : gem_init(xo));
}


/* ----------------------------------------------------- */
/* 資料之修改：edit / title				 */
/* ----------------------------------------------------- */


static int
gem_edit(xo)
  XO *xo;
{
  char fpath[80];
  HDR *hdr;

  if(bbsothermode & OTHERSTAT_EDITING)
  {
    vmsg("你還有檔案還沒編完哦！");
    return XO_FOOT;
  }  

  if (!(hdr = gem_check(xo, fpath, GEM_WRITE | GEM_FILE)))
    return XO_NONE;
  if ((hdr->xmode & GEM_RESERVED) && (xo->key < GEM_SYSOP))
    return XO_NONE;
  if (vedit(fpath, NA) >= 0) /* Thor.981020: 注意被talk的問題 */
    gem_log(xo->dir, "修改", hdr);
  return gem_head(xo);
}


static int
gem_title(xo)
  XO *xo;
{
  HDR *ghdr, xhdr;
  int num;
  char *dir;

  ghdr = gem_check(xo, NULL, GEM_WRITE);
  if (ghdr == NULL)
    return XO_NONE;

  xhdr = *ghdr;
  vget(b_lines, 0, "標題：", xhdr.title, TTLEN + 1, GCARRY);

  dir = xo->dir;
  if (xhdr.xmode & GEM_GOPHER)
  {
    if (url_edit(dir, &xhdr))
      return XO_FOOT;
  }
  else
  {
    if (cuser.userlevel & (PERM_ALLBOARD|PERM_GEM))
    {
      vget(b_lines, 0, "編者：", xhdr.owner, IDLEN + 2, GCARRY);
      vget(b_lines, 0, "時間：", xhdr.date, 9, GCARRY);
    }
  }

  if (memcmp(ghdr, &xhdr, sizeof(HDR)) &&
    vans("確定要修改嗎(Y/N)？[N]") == 'y')
  {
    *ghdr = xhdr;
    num = xo->pos;
    rec_put(dir, ghdr, sizeof(HDR), num);
    num++;
    move(num - xo->top + 2, 0);
    gem_item(num, ghdr);

    gem_log(xo->dir, "標題", ghdr);
  }
  return XO_FOOT;
}


static int
gem_lock(xo)
  XO *xo;
{
  HDR *ghdr;
  int num;

  if(!HAS_PERM(PERM_SYSOP))
    return XO_NONE;

  if ((ghdr = gem_check(xo, NULL, GEM_WRITE)))
  {
    ghdr->xmode ^= GEM_LOCK;

    num = xo->pos;
    rec_put(xo->dir, ghdr, sizeof(HDR), num);
    num++;
    move(num - xo->top + 2, 0);
    gem_item(num, ghdr);
  }

  return XO_NONE;
}


static int
gem_mark(xo)
  XO *xo;
{
  HDR *ghdr;
  int num;

  if ((ghdr = gem_check(xo, NULL, GEM_WRITE)))
  {
    ghdr->xmode ^= GEM_RESTRICT;

    num = xo->pos;
    rec_put(xo->dir, ghdr, sizeof(HDR), num);
    num++;
    move(num - xo->top + 2, 0);
    gem_item(num, ghdr);
  }

  return XO_NONE;
}


static int
gem_state(xo)
  XO *xo;
{
  HDR *ghdr;
  char *dir, fpath[80], site[64], path[512], *str;
  struct stat st;
  int bno;

  /* Thor.990107: Ernie patch: 
    gem.c gem_browse() 在進入 gopher(絲路)的 folder 時一律 op = GEM_VISIT
    使得板主只能在 gopher 最外層觀看檔案屬性及 update proxy，進入 gopher
    便失效。

    解決辦法: 進 gem_state() 時多判斷是否為該板板主，有更好的方式請指正 :)
  */
  if (!(bbstate & STAT_BOARD) && xo->key <= GEM_USER && !(HAS_PERM(PERM_SYSOP))) 
    return XO_NONE;
    

  if(!(ghdr = gem_check(xo, fpath, GEM_READ)))
    return XO_NONE;
  /* Thor.980216: 注意! 有可能傳回 NULL導至踢人 */
  /* Thor.990415: 此情況為,連不到對方,url_fpath回傳-1,則gem_check會回傳NULL */

  if(!(HAS_PERM(PERM_ALLBOARD)))
  {
    if(!str_ncmp(fpath,"gem/brd/",8))
    {
      dir = fpath + 8;
      if((str = strchr(dir,'/')))
      {
        *str = '\0';
        bno = brd_bno(dir);
        *str = '/';
        if(bno < 0 || !(brd_bits[bno] & BRD_X_BIT))
          return XO_NONE;
      }
      else
        return XO_NONE;
    }
    else
      return XO_NONE;
  }

  dir = xo->dir;

  move(12, 0);
  clrtobot();
  outs("\nDir : ");
  outs(dir);
  outs("\nName: ");
  outs(ghdr->xname);
  outs("\nFile: ");
  outs(fpath);

  if (!stat(fpath, &st))
    prints("\nTime: %s\nSize: %d", Ctime(&st.st_mtime), st.st_size);

  if (ghdr->xmode & GEM_GOPHER)
  {
    url_parse(dir, ghdr, site, path);
    outs("\n\nHost: ");
    outs(site);
    outs("\nPath: ");
    outs(path);
    prints("\nPort: %d", ghdr->xid);

    if (vans("是否清理 proxy，重抓資料(Y/N)？[N]") == 'y')
      unlink(fpath);
  }
  else
  {
    vmsg(NULL);
  }

  return gem_body(xo);
}


/* ----------------------------------------------------- */
/* 資料之瀏覽：edit / title				 */
/* ----------------------------------------------------- */


static int
gem_browse(xo)
  XO *xo;
{
  HDR *ghdr;
  int op, xmode;
  char fpath[80], title[TTLEN + 1], *ptr;

  op = GEM_READ;

  do
  {
    ghdr = gem_check(xo, fpath, op);
    if (ghdr == NULL)
      break;

    xmode = ghdr->xmode;

    /* browse folder */

    if (xmode & GEM_FOLDER)
    {
      op = xo->key;
      if (xmode & GEM_BOARD)
      {
	op = brd_bno(ghdr->xname);
        if (HAS_PERM(PERM_SYSOP|PERM_BOARD|PERM_GEM)) /* visor.991119: 我還是給看版總管 GEM_SYSOP */
          op = GEM_SYSOP;
	else if ((op >= 0) && (brd_bits[op] & BRD_X_BIT))
	  op = GEM_MANAGER;
    else if (ptr = strrchr(ghdr->title, '['))
      op = GEM_MANAGER;
	else
	  op = GEM_USER;
      }
      else if (xmode & HDR_URL)
      {
	op = GEM_VISIT;
      }

      strcpy(title, ghdr->title);

      if(gem_manage(title))
        op = GEM_LMANAGER;

      XoGem(fpath, title, op);
      return gem_init(xo);
    }

    /* browse article */

    /* Thor.990204: 為考慮more 傳回值 */   
    if ((xmode = more(fpath, MSG_GEM)) == -2)
      return XO_INIT;
    if(xmode == -1)
      break;

    op = GEM_READ | GEM_FILE;

    xmode = xo_getch(xo, xmode);

  } while (xmode == XO_BODY);

  if (op != GEM_READ)
    gem_head(xo);
  return XO_NONE;
}


/* ----------------------------------------------------- */
/* 精華區之刪除： copy / cut (delete) paste / move	 */
/* ----------------------------------------------------- */


static char GemFolder[80], GemAnchor[80], GemSailor[24];
static HDR *GemBuffer;
static int GemBufferSiz; /* , GemBufferNum; */ 
                         /* Thor.990414: 提前宣告給gem_head用 */


/* 配置足夠的空間放入 header */


static HDR *
gbuf_malloc(num)
  int num;
{
  HDR *gbuf;

  GemBufferNum = num;
  if ((gbuf = GemBuffer))
  {
    if (GemBufferSiz < num)
    {
      num += (num >> 1);
      GemBufferSiz = num;
      GemBuffer = gbuf = (HDR *) realloc(gbuf, sizeof(HDR) * num);
    }
  }
  else
  {
    GemBufferSiz = num;
    GemBuffer = gbuf = (HDR *) malloc(sizeof(HDR) * num);
  }

  return gbuf;
}


static void
gem_buffer(dir, ghdr)
  char *dir;
  HDR *ghdr;			/* NULL 代表放入 TagList, 否則將傳入的放入 */
{
  int num, locus;
  HDR *gbuf;

  if (ghdr)
  {
    num = 1;
  }
  else
  {
    num = TagNum;
    if (num <= 0)
      return;
  }

  gbuf = gbuf_malloc(num);

  if (ghdr)
  {
    memcpy(gbuf, ghdr, sizeof(HDR));
  }
  else
  {
    locus = 0;
    do
    {
      EnumTagHdr(&gbuf[locus], dir, locus);
    } while (++locus < num);
  }

  strcpy(GemFolder, dir);
}


static inline void
gem_store()
{
  int num;
  char *folder;

  num = GemBufferNum;
  if (num <= 0)
    return;

  folder = GemFolder;
  str_folder(folder, folder, FN_GEM);

  rec_add(folder, GemBuffer, sizeof(HDR) * num);
}


static int
gem_delete(xo)
  XO *xo;
{
  HDR *ghdr;
  char *dir, buf[80];
  int tag;

  if (!(ghdr = gem_check(xo, NULL, GEM_WRITE)))
    return XO_NONE;

  tag = AskTag("精華區刪除");

  if (tag < 0)
    return XO_FOOT;

  if (tag > 0)
  {
    sprintf(buf, "確定要刪除 %d 篇標籤精華嗎(Y/N)？[N] ", tag);
    if (vans(buf) != 'y')
      return XO_FOOT;
  }

  dir = xo->dir;

  gem_buffer(dir, tag ? NULL : ghdr);

  if (xo->key > GEM_RECYCLE &&
    vans("是否放進資源回收筒(Y/N)？[N] ") == 'y')
    gem_store();

  /* 只刪除 HDR 並不刪除檔案 */

  if (tag)
  {
    int fd;
    FILE *fpw;

    if ((fd = open(dir, O_RDONLY)) < 0)
      return XO_FOOT;

    if (!(fpw = f_new(dir, buf)))
    {
      close(fd);
      return XO_FOOT;
    }

    mgets(-1);
    tag = 0;

    while ((ghdr = mread(fd, sizeof(HDR))))
    {
      if (Tagger(ghdr->chrono, tag, TAG_NIN))
      {
	if ((fwrite(ghdr, sizeof(HDR), 1, fpw) != 1))
	{
	  fclose(fpw);
	  unlink(buf);
	  close(fd);
	  return XO_FOOT;
	}
      }
      else
      {
	gem_log(dir, "刪除", ghdr);
      }
      tag++;
    }
    close(fd);
    fclose(fpw);
    rename(buf, dir);
    TagNum = 0;
  }
  else
  {
    currchrono = ghdr->chrono;
    rec_del(dir, sizeof(HDR), xo->pos, (void *) cmpchrono, NULL);
    gem_log(dir, "刪除", ghdr);
  }

  /* return gem_load(xo); */
  return gem_init(xo); /* Thor.990414: 為了將剪貼簿篇數一併反應 */
}


static int
gem_copy(xo)
  XO *xo;
{
  HDR *ghdr;
  int tag;

  ghdr = gem_check(xo, NULL, GEM_WRITE);
  if (ghdr == NULL)
    return XO_NONE;

  tag = AskTag("精華區拷貝");

  if (tag < 0)
    return XO_FOOT;

  gem_buffer(xo->dir, tag ? NULL : ghdr);

  zmsg("拷貝完成");
  /* return XO_NONE; */
  return XO_HEAD; /* Thor.990414: 讓剪貼篇數更新 */
}


static inline int
gem_extend(xo, num)
  XO *xo;
  int num;
{
  char *dir, fpath[80], gpath[80];
  FILE *fp;
  time_t chrono;
  HDR *hdr;

  if (!(hdr = gem_check(xo, fpath, GEM_WRITE | GEM_FILE)))
    return -1;

  if (!(fp = fopen(fpath, "a")))
    return -1;

  dir = xo->dir;
  chrono = hdr->chrono;

  for (hdr = GemBuffer; num--; hdr++)
  {
    if ((hdr->chrono != chrono) && !(hdr->xmode & GEM_FOLDER))
    {
      hdr_fpath(gpath, dir, hdr);	/* Thor: 假設 hdr和 xo->dir是同一目錄 */
      fputs(STR_LINE, fp);
      f_suck(fp, gpath);
    }
  }

  fclose(fp);
  return 0;
}


static int
gem_paste(xo)
  XO *xo;
{
  int num, ans;
  char *dir, srcDir[80], dstDir[80];

  /* 是否產生 cycle ? */

  if (xo->key <= GEM_USER)
    return XO_NONE;

  if (!(num = GemBufferNum))
  {
    zmsg("請先執行 copy 命令後再 paste");
    return XO_NONE;
  }

  str_folder(srcDir, GemFolder, FN_GEM);
  str_folder(dstDir, dir = xo->dir, FN_GEM);

  if (strcmp(srcDir, dstDir))
  {
    zmsg("目前不支援[跨區拷貝]");
    return XO_NONE;
  }

  switch (ans = vans("存放位置 A)ppend I)nsert N)ext E)xtend Q)uit [A] "))
  {
  case 'q':
    return XO_FOOT;

  case 'e':
    if (gem_extend(xo, num))
    {
      zmsg("[Extend 檔案附加] 動作並未完全成功\");
      return XO_NONE;
    }
    /* Thor.990105: 成功則清除訊息 */
    return XO_FOOT;

  case 'i':
  case 'n':
    rec_ins(dir, GemBuffer, sizeof(HDR), xo->pos + (ans == 'n'), num);
    break;

  default:
    rec_add(dir, GemBuffer, sizeof(HDR) * num);
  }

  return gem_load(xo);
}


static int
gem_move(xo)
  XO *xo;
{
  HDR *ghdr;
  char *dir, buf[80];
  int pos, newOrder;

  ghdr = gem_check(xo, NULL, GEM_WRITE);

  if (ghdr == NULL)
    return XO_NONE;

  pos = xo->pos;
  sprintf(buf + 5, "請輸入第 %d 選項的新位置：", pos + 1);
  if (!vget(b_lines, 0, buf + 5, buf, 5, DOECHO))
    return XO_FOOT;

  newOrder = atoi(buf) - 1;
  if (newOrder < 0)
    newOrder = 0;
  else if (newOrder >= xo->max)
    newOrder = xo->max - 1;

  if (newOrder != pos)
  {

#if 0
    if (!rec_mov(xo->dir, sizeof(HDR), pos, newOrder))
      return XO_LOAD;
#else
    dir = xo->dir;
    if (!rec_del(dir, sizeof(HDR), pos, NULL, NULL))
    {
      rec_ins(dir, ghdr, sizeof(HDR), newOrder, 1);
      xo->pos = newOrder;
      return XO_LOAD;
    }
#endif
  }
  return XO_FOOT;
}


static int
gem_recycle(xo)
  XO *xo;
{
  int level;
  char fpath[64];

  level = xo->key;

  if (level <= GEM_USER)
    return XO_NONE;

  if (level == GEM_LMANAGER)
  {
    vmsg("小板主不能進入回收筒！");
    return XO_NONE;
  }

  if (level == GEM_RECYCLE)
  {
    if (vans("確定要清理資源回收筒嗎[Y/N]?(N) ") == 'y')
    {
      unlink(xo->dir);
      return XO_QUIT;
    }
    return XO_FOOT;
  }

  str_folder(fpath, xo->dir, FN_GEM);
  if (rec_num(fpath, sizeof(HDR)) <= 0)
  {
    zmsg("資源回收筒並無資料");
    return XO_NONE;
  }

  XoGem(fpath, "● 資源回收筒 ●", GEM_RECYCLE);
  return XO_INIT;
}


static int
gem_anchor(xo)
  XO *xo;
{
  int ans;
  char *folder;

  /* Thor.981020: 規定資源回收筒不得定錨, 另, 拔錨後, g則會丟入回收筒 */
  if (xo->key < GEM_LMANAGER)  /* Thor.981020: 只要版主以上即可使用anchor */
    return XO_NONE; 
  /* Thor.981020: 不開放一般user使用是為了防止版主試出另一個小bug:P */

  ans = vans("精華區 A)定錨 D)拔錨 J)就位 Q)取消 [J] ");
  if (ans != 'q')
  {
    folder = GemAnchor;

    if (ans == 'a')
    {
      strcpy(folder, xo->dir);
      str_ncpy(GemSailor, xo->xyz, sizeof(GemSailor));
    }
    else if (ans == 'd')
    {
      *folder = '\0';
    }
    else
    {
      if (!*folder)
	return gem_recycle(xo);

      XoGem(folder, "● 精華定錨區 ●", xo->key);
      return XO_INIT;
    }

    zmsg("錨動作完成");
  }

  return XO_NONE;
}


int
gem_gather(xo)
  XO *xo;
{
  HDR *hdr, *gbuf, ghdr, xhdr;
  int tag, locus, rc, xmode, anchor,mode;
  char *dir, *folder, *msg, fpath[80], buf[80];
  FILE *fp,*fd;

  folder = GemAnchor;
  if ((anchor = *folder))
  {
    msg = buf;
    sprintf(msg, "收錄至精華定錨區 (%s)", GemSailor);
  }
  else
  {
    msg = "收錄至精華回收筒";
  }
  tag = AskTag(msg);

  if (tag < 0)
    return XO_FOOT;

  if (!anchor)
  {
    sprintf(folder, "gem/brd/%s/%s", currboard, FN_GEM);
  }

  fd = fp = NULL;

  if(vans("附加作者 ID [y/N] ") == 'y')
    mode = 1;
  else
    mode = 0;

  dir = xo->dir;

  if (tag)
  {
    hdr = &xhdr;
    EnumTagHdr(hdr, dir, 0);
  }
  else
    hdr = (HDR *) xo_pool + xo->pos - xo->top; 
 
  if(hdr->xmode & POST_DELETE)
    return XO_FOOT;


  if (tag > 0)
  {
    switch (vans("串列文章 1)合成一篇 2)分別建檔 Q)取消 [2] "))
    {
    case 'q':
      return XO_FOOT;

    case '1':
      if (!vget(b_lines, 0, "標題：", xhdr.title, TTLEN + 1, GCARRY))
	return XO_FOOT;
      fp = fdopen(hdr_stamp(folder, 'A', &ghdr, fpath), "w");
      strcpy(ghdr.owner, cuser.userid);
      strcpy(ghdr.title, xhdr.title);
      break;
    default:
      break;

    }
  }

  rc = (*dir == 'b') ? XO_LOAD : XO_FOOT;

  /* gather 視同 copy, 可準備作 paste */

  strcpy(GemFolder, folder);
  gbuf = gbuf_malloc((fp != NULL || tag == 0) ? 1 : tag);

  locus = 0;

  do
  {
    if (tag)
    {
      EnumTagHdr(hdr, dir, locus);
    }

    xmode = hdr->xmode;
    
    /* Thor.981018: 特別注意, anchor 沒法收錄 folder, 因程式不好寫(跨區copy),
                    此時同區的folder可以用copy & paste,
                    不同區就只好一篇篇收錄 */
    if (!(xmode & (GEM_FOLDER|POST_DELETE)))	/* 查 hdr 是否 plain text */
    {
      if (xmode & HDR_URL)
	url_fpath(fpath, dir, hdr);
      else
	hdr_fpath(fpath, dir, hdr);

      if (fp)
      {
	f_suck(fp, fpath);
	fputs(STR_LINE, fp);
      }
      else
      {
        /*strcpy(buf,fpath);*/
        fd = fdopen(hdr_stamp(folder, 'A', &ghdr, buf), "w"); /*by visor*/
	/*hdr_stamp(folder, HDR_LINK | 'A', &ghdr, fpath);*/
	strcpy(ghdr.owner, cuser.userid);
	if(mode)
        {
          char tmp[80],*ptr;
          strcpy(tmp,hdr->owner);
          ptr = strchr(tmp,'.');
          if(ptr)
            *ptr = '\0';
          ptr = strchr(tmp,'@'); 
          if(ptr)
            *ptr = '\0';
		  strncpy(ghdr.title, hdr->title,sizeof(ghdr.title) - IDLEN - 4);
		  strcat(ghdr.title," (");
		  strcat(ghdr.title,tmp);
		  strcat(ghdr.title,")");
//          sprintf(ghdr.title,"(%s)",tmp);
//          strncat(ghdr.title, hdr->title,sizeof(ghdr.title) - strlen(ghdr.title) - 1);
        }
        else
        {
          strcpy(ghdr.title, hdr->title);
        }
        f_suck(fd, fpath);
	rec_add(folder, &ghdr, sizeof(HDR));
	gem_log(folder, "新增", &ghdr);

        if(fd>=0)                 /* by visor */
          fclose(fd);

	gbuf[locus] = ghdr;	/* 放入 Gembuffer */
      }

      if ((rc == XO_LOAD) && !(xmode & POST_GEM))
      {
	/* update 屬性 */

	hdr->xmode = xmode | POST_GEM;
	rec_put(dir, hdr, sizeof(HDR), tag ? TagList[locus].recno :
	  (xo->key == XZ_POST ? xo->pos : hdr->xid));
      }
    }
  } while (++locus < tag);

  if (fp)
  {
    fclose(fp);
    gbuf[0] = ghdr;
    rec_add(folder, &ghdr, sizeof(HDR));
    gem_log(folder, "新增", &ghdr);
  }

  zmsg("收錄完成");

  return rc;			/* Thor: 清除上面顯示的訊息 */
}


static int
gem_tag(xo)
  XO *xo;
{
  HDR *ghdr;
  int pos, tag;

  if ((ghdr = gem_check(xo, NULL, GEM_READ)) && (tag = Tagger(ghdr->chrono, pos = xo->pos, TAG_TOGGLE)))
  {
    move(3 + pos - xo->top, 7);
    outc(tag > 0 ? '*' : ' '); 
  }
   
  /* return XO_NONE; */
  return xo->pos + 1 + XO_MOVE; /* lkchu.981201: 跳至下一項 */
}


static int
gem_help(xo)
  XO *xo;
{
  film_out(FILM_GEM, -1);
  return gem_head(xo);
}

static int
gem_cross(xo)
  XO *xo;
{
  char xboard[20], fpath[80], xfolder[80], xtitle[80], buf[80], *dir;
  HDR *hdr, xpost,*ghdr;
#ifdef  HAVE_DETECT_CROSSPOST
  HDR bpost;
#endif  
  int method=1, rc, tag, locus, battr;
  FILE *xfp;

  if (!cuser.userlevel)
    return XO_NONE;

  ghdr = gem_check(xo, NULL, GEM_READ);
  tag = AskTag("轉貼");
  if ((tag < 0) || (tag==0 && (ghdr->xmode & GEM_FOLDER)))
    return XO_FOOT;


  if (ask_board(xboard, BRD_W_BIT,
      "\n\n\033[1;33m請挑選適當的看板，切勿轉貼超過三板。\033[m\n\n"))
  {
    if (*xboard == 0)
      strcpy(xboard, currboard);

    hdr = tag ? &xpost : (HDR *) xo_pool + (xo->pos - xo->top);

          
    if (!tag)   
    {
      if(!(hdr->xmode & GEM_FOLDER) && !((hdr->xmode & (GEM_RESTRICT|GEM_RESERVED)) && (xo->key < GEM_MANAGER))
         && !((hdr->xmode & GEM_LOCK) && !HAS_PERM(PERM_SYSOP)))
      {
        sprintf(xtitle, "[轉錄]%.66s", hdr->title);
        if (!vget(2, 0, "標題:", xtitle, TTLEN + 1, GCARRY))
          return XO_HEAD;
      }
      else
        return XO_HEAD;          
    }
    
    rc = vget(2, 0, "(S)存檔 (Q)取消？[Q] ", buf, 3, LCECHO);
    if (*buf != 's' && *buf != 'S')
      return XO_HEAD;
      
    locus = 0;
    dir = xo->dir;

    battr = (bshm->bcache + brd_bno(xboard))->battr; 

    do	
    {
      if (tag)
      {
        EnumTagHdr(hdr, dir, locus++);

        sprintf(xtitle, "[轉錄]%.66s", hdr->title);
      }
      if (!(hdr->xmode & GEM_FOLDER) && !((hdr->xmode & (GEM_RESTRICT|GEM_RESERVED)) && (xo->key < GEM_MANAGER))
           && !((hdr->xmode & GEM_LOCK) && !HAS_PERM(PERM_SYSOP)))
      {
        xo_fpath(fpath, dir, hdr);     
        brd_fpath(xfolder, xboard, fn_dir);

  	method = hdr_stamp(xfolder, 'A', &xpost, buf);
  	xfp = fdopen(method, "w");

  	strcpy(ve_title, xtitle);
  	strcpy(buf, currboard);
  	strcpy(currboard, xboard);

  	ve_header(xfp);

  	strcpy(currboard, buf);

        strcat(buf, "] 精華區");
  	fprintf(xfp, "※ 本文轉錄自 [%s\n\n", buf);

  	f_suck(xfp, fpath);
  	fclose(xfp);
  	close(method);

    	strcpy(xpost.owner, cuser.userid);
        strcpy(xpost.nick, cuser.username);
        

        strcpy(xpost.title, xtitle);

        rec_bot(xfolder, &xpost, sizeof(xpost));

#ifdef  HAVE_DETECT_CROSSPOST
        memcpy(&bpost,hdr,sizeof(HDR));
        if(checksum_find(fpath,0,battr))
        {
          strcpy(bpost.owner,cuser.userid);
          add_deny(&cuser,DENY_SEL_POST|DENY_DAYS_1|DENY_MODE_POST,0);
          deny_log_email(cuser.vmail,(cuser.userlevel & PERM_DENYSTOP) ? -1 : cuser.deny);
          bbstate &= ~STAT_POST;
          cuser.userlevel &= ~PERM_POST;

          move_post(&bpost,BRD_VIOLATELAW,-2);
          
          board_main();
        }        
#endif
      }
    } while (locus < tag);
       
    if ( battr & BRD_NOCOUNT)
    {
      outs("轉錄完成，文章不列入紀錄，敬請包涵。");
    }
    else
    {
      cuser.numposts += (tag == 0) ? 1 : tag; 
      vmsg("轉錄完成");
    }
  }
  return XO_HEAD;
}

static KeyFunc gem_cb[] =
{
  {XO_INIT, gem_init},
  {XO_LOAD, gem_load},
  {XO_HEAD, gem_head},
  {XO_BODY, gem_body},

  {'r', gem_browse},

  {Ctrl('P'), gem_add},
  {'E', gem_edit},
  {'T', gem_title},
  {'m', gem_mark},
  {'x', gem_cross},
  {'l', gem_lock},

  {'d', gem_delete},
  {'c', gem_copy},

  {Ctrl('G'), gem_anchor},
  {Ctrl('V'), gem_paste},

  {'t', gem_tag},
  {'f', gem_toggle},
  {'M', gem_move},

  {'S', gem_state},

  {Ctrl('W'), gem_recycle},


  {'h', gem_help}
};


void
XoGem(folder, title, level)
  char *folder;
  char *title;
  int level;
{
  XO *xo, *last;

  last = xz[XZ_GEM - XO_ZONE].xo;	/* record */

  xz[XZ_GEM - XO_ZONE].xo = xo = xo_new(folder);
  xo->pos = 0;
  xo->key = level;
  xo->xyz = title;

  xover(XZ_GEM);

  free(xo);

  xz[XZ_GEM - XO_ZONE].xo = last;	/* restore */
}


void
gem_main()
{
  XO *xo;

  xz[XZ_GEM - XO_ZONE].xo = xo = xo_new("gem/.DIR");
  xz[XZ_GEM - XO_ZONE].cb = gem_cb;
  xo->pos = 0;
  xo->key = ((HAS_PERM(PERM_SYSOP|PERM_BOARD|PERM_GEM)) ? GEM_SYSOP : GEM_USER);
  xo->xyz = "";
}
