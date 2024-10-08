#ifdef ENABLE_PAGE
    #include "page/include_page.h"
#endif

#ifdef ENABLE_LIBC
    #define _GNU_SOURCE
    #include <arpa/inet.h>
    #include <errno.h>
    #include <stdio.h>
    #include <string.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <limits.h>
    #include <sys/stat.h>
    #include <stdlib.h>
    #include <dirent.h>
    #include <stdarg.h>
    #include <time.h>
    #include <strings.h>

#else
    #include "include/nolibc.h"
#endif

#ifdef ENABLE_LOG
    #define Error(...) fprintf(stderr, __VA_ARGS__)
    #define Report(...) fprintf(stderr, __VA_ARGS__)
#else
    #define Error(...)
    #define Report(...)
#endif

#define PORT 8080
#define BUFFER_SIZE 1024*8
#define PB_DeclareString( name, size, lit ) char name ## _buffer[size] = lit; printbuffer_t name = { name ## _buffer, sizeof(lit) - 1, size }
#define PB_Declare( name, size ) char name ## _buffer[size] = "";printbuffer_t name = { name ## _buffer, 0, size - 1 };
typedef struct printbuffer_s
{
	char *buf;
	size_t pos;
	size_t sz;
} printbuffer_t;
void PB_PrintString( printbuffer_t *pb, const char *fmt, ... );
#if !defined(ENABLE_LIBC) && defined(ENABLE_LOG)
static char global_printbuf_buffer[1024];
static printbuffer_t global_printbuf = {global_printbuf_buffer, 0, 1024};
#endif

static int writeall(int fd, const char *resp, size_t len)
{
	size_t sent = 0;
	do
	{
		int res = write(fd, resp + sent, len - sent);
		if( res >= 0)
			sent += res;
		else
			return res;
	}
	while(sent < len);

	return sent;
}

static int readheaders(int fd, char *buf, size_t len, int *headerend)
{
	size_t received = 0;
	do
	{
		int res = read(fd, &buf[received], len - received);
		if( res >= 0)
		{
			char *headend;
			int he1 = INT_MAX, he2 = INT_MAX;
			buf[received + res] = 0;
			headend = strstr(buf, "\n\n");
			if(headend)
				he1 = headend - buf + 2;
			headend = strstr(buf, "\r\n\r\n");
			if(headend)
				he2 = headend - buf + 4;

			received += res;
			if(he2 < he1) he1  =he2;
			if(he1 != INT_MAX)
			{
				*headerend = he1;
				return received;
			}
		}
		else
			return res;
	}
	while(received < len);

	return -1;
}

#define WriteStringLit( fd, lit ) writeall( fd, lit, sizeof( lit ) - 1);

static int ReadAll(int fd, char *data, size_t len)
{
	size_t received = 0;
	do
	{
		int res = recv(fd, data + received, len - received, 0);
		if( res >= 0)
			received += res;
		else
			return res;
	}
	while(received < len);

	return received;
}

static int DumpAll(int fd, int outfd, char *buffer, size_t bufsize, size_t len )
{
	size_t received = 0;
	do
	{
		int rsize = bufsize;
		int res;

		if( rsize > len - received ) rsize = len - received;
		res = recv(fd, buffer, rsize, 0);
		if( res >= 0)
		{
			received += res;
			write( outfd, buffer, res );
		}
		else
			return res;
	}
	while(received < len);

	return received;
}

static int SkipAll(int fd, char *buffer, size_t bufsize, size_t len )
{
	size_t received = 0;
	do
	{
		int rsize = bufsize;
		int res;

		if( rsize > len - received ) rsize = len - received;
		res = recv(fd, buffer, rsize, 0);
		if( res >= 0)
		{
			received += res;
		}
		else
			return res;
	}
	while(received < len);

	return received;
}


#define READ_BUFFER_SIZE BUFFER_SIZE
static char read_buffer[READ_BUFFER_SIZE];
static struct
{
	size_t read_offset, ahead_offset;
	int fd;
}	rbstate;

static int RB_Read( char *out, size_t len )
{
	// flush alreade read data
	int availiable = rbstate.ahead_offset - rbstate.read_offset;

	if( availiable > len )
		availiable = len;
	memcpy( out, &read_buffer[rbstate.read_offset],  availiable );

	len -= availiable;
	out += availiable;

	rbstate.read_offset += availiable;

	if( rbstate.ahead_offset == rbstate.read_offset ) // do not need any data in buffer, may reset buffer to beginning
		rbstate.ahead_offset = rbstate.read_offset = 0;

	if(len == 0)
		return availiable;
	else
	{
		int rd = ReadAll( rbstate.fd, out, len );
		if( rd < 0) return rd;
		return rd + availiable;
	}
}

static int RB_Dump( int fd, size_t len )
{
	// flush alreade read data
	int availiable = rbstate.ahead_offset - rbstate.read_offset;

	if( availiable > len )
		availiable = len;

	write( fd, &read_buffer[rbstate.read_offset],  availiable );

	len -= availiable;

	rbstate.read_offset += availiable;

	if( rbstate.ahead_offset == rbstate.read_offset ) // do not need any data in buffer, may reset buffer to beginning
		rbstate.ahead_offset = rbstate.read_offset = 0;

	if(len == 0)
		return availiable;
	else
	{
		int rd = DumpAll( rbstate.fd, fd, &read_buffer[rbstate.ahead_offset], READ_BUFFER_SIZE - rbstate.ahead_offset, len );
		if( rd < 0) return rd;
		return rd + availiable;
	}
}

static int RB_Skip( size_t len )
{
	// flush alreade read data
	int availiable = rbstate.ahead_offset - rbstate.read_offset;

	if( availiable > len )
		availiable = len;

	len -= availiable;

	rbstate.read_offset += availiable;

	if( rbstate.ahead_offset == rbstate.read_offset ) // do not need any data in buffer, may reset buffer to beginning
		rbstate.ahead_offset = rbstate.read_offset = 0;

	if(len == 0)
		return availiable;
	else
	{
		int rd = SkipAll( rbstate.fd, &read_buffer[rbstate.ahead_offset], READ_BUFFER_SIZE - rbstate.ahead_offset, len );
		if( rd < 0) return rd;
		return rd + availiable;
	}
}


static void RB_Init( int fd )
{
	rbstate.fd = fd;
	rbstate.ahead_offset = rbstate.read_offset = 0;
}
#define METHOD_LEN 32
#define URI_LEN 1024


static int RB_ReadAhead( int force )
{
	int res = 1;

	if(rbstate.ahead_offset == rbstate.read_offset || force )
	{
		res = read( rbstate.fd, &read_buffer[rbstate.ahead_offset], READ_BUFFER_SIZE - rbstate.ahead_offset - 1 );

		if(res < 0)
			return res;

		read_buffer[rbstate.ahead_offset += res] = 0;
	}

	return res;
}

static int RB_ReadLine( char *out, size_t maxlen )
{
	int res = 0;
	maxlen--;
	do
	{
		char *lineend;

		res = RB_ReadAhead(res != 0);
		if(res < 0)
			return res;

		lineend = strchr( &read_buffer[rbstate.read_offset], '\n' );
		if( lineend )
		{
			int linelen = ++lineend - &read_buffer[rbstate.read_offset];
			if(maxlen > linelen - 1 ) maxlen = linelen - 1;
			memcpy( out, &read_buffer[rbstate.read_offset], maxlen );
			out[maxlen] = 0;
			rbstate.read_offset += linelen;
			return linelen;
		}
	} while( res > 0 );
	return -1;
}

static int RB_SkipLine( void )
{
	int res = 0;
	do
	{
		char *lineend;

		res = RB_ReadAhead(res != 0);
		if(res < 0)
			return res;

		lineend = strchr( &read_buffer[rbstate.read_offset], '\n' );
		if( lineend )
		{
			int linelen = ++lineend - &read_buffer[rbstate.read_offset];
			rbstate.read_offset += linelen;
			return linelen;
		}
	} while( res > 0 );
	return -1;
}

static size_t S_strncpy(char *dest, const char *src, size_t dmax)
{
	size_t smax = dmax - 1;
	char *orig_dest = dest;
	if( dmax < 1 )
		return 0;

	while (dmax > 0) {

		*dest = *src; /* Copy the data into the destination */

		/* Check for maximum copy from source */
		if (smax == 0) {
			/* we have copied smax characters, add null terminator */
			*dest = '\0';
		}

		/* Check for end of copying */
		if (*dest == '\0') {
			return dest - orig_dest;
		}
		dmax--;
		smax--;
		dest++;
		src++;
	}

	return dest - orig_dest;
}

// does not do buffer wrapping, will fail if headers not fit
static int RB_ReadHeaders( char *method, char *uri, char *headers, size_t hlen )
{
	int res;
	// read first line
	do
	{
		char *lineend;
		res = RB_ReadAhead(1);

		if(res < 0)
			return res;

		lineend = strchr( &read_buffer[rbstate.read_offset], '\n' );
		if( lineend )
		{
			char *space = strchr(&read_buffer[rbstate.read_offset], ' ');
			if( space )
			{
				unsigned int len = space - &read_buffer[rbstate.read_offset] + 1;
				char *space2;
				if(len > 31) len = 31;
				S_strncpy( method, &read_buffer[rbstate.read_offset], len );
				space++;
				space2 = strchr( space, ' ');
				if(space2)
				{
					len = space2 - space + 1;
					if(len > 1023) len = 1023;
					S_strncpy( uri, space, len);
				}
				else space = NULL;
			}
			if(!space)
			{
				Error("Bad headers!\n");
				return -1;
			}
			rbstate.read_offset = lineend - &read_buffer[rbstate.read_offset];

			break;
		}
	} while( res > 0 );

	res = 0;
	// read all headers until empty line;
	do
	{
		char *headend;
		res = RB_ReadAhead(res != 0);

		int he1 = INT_MAX, he2 = INT_MAX;

		if(res < 0)
			return res;

		headend = strstr(&read_buffer[rbstate.read_offset], "\n\n");
		if(headend)
			he1 = headend - &read_buffer[rbstate.read_offset] + 2;
		headend = strstr(&read_buffer[rbstate.read_offset], "\r\n\r\n");
		if(headend)
			he2 = headend - &read_buffer[rbstate.read_offset] + 4;

		if(he2 < he1) he1 = he2;
		if(he1 != INT_MAX)
		{
			if(hlen > he1) hlen = he1;
			memcpy( headers, &read_buffer[rbstate.read_offset], hlen );
			headers[hlen] = 0;
			rbstate.read_offset += he1;
			return hlen;
		}
	} while( res > 0 );

	return -1;
}


static void PB_Init( printbuffer_t *pb, char *buf, size_t buflen )
{
	pb->buf = buf;
	pb->sz = buflen - 1; // always null-terminate
	pb->pos = 0;
	pb->buf[pb->sz] = 0;
}

static void PB_WriteString( printbuffer_t *pb, const char *str )
{
	int len = S_strncpy( pb->buf + pb->pos, str, pb->sz - pb->pos );
	pb->pos += len;
	if( pb->pos > pb->sz )
	{
		pb->pos = pb->sz;
		Error("PB: String buffer overflow!\n");
	}
}

static void PB_WriteStringLen( printbuffer_t *pb, const char *str, size_t len )
{
	if( len > pb->sz - pb->pos )
		len = pb->sz - pb->pos;

	memcpy( pb->buf + pb->pos, str, len );
	pb->pos += len;
}

#define PB_WriteStringLit(p, x) PB_WriteStringLen(p, x, sizeof( x ) - 1)

void PB_PrintString( printbuffer_t *pb, const char *fmt, ... )
{
	int res;
	va_list args;

	va_start( args, fmt );
	res = vsnprintf( pb->buf + pb->pos, pb->sz - pb->pos, fmt, args );
	va_end( args );
	if( res > 0 )
		pb->pos += res;

	if( pb->pos > pb->sz )
	{
		pb->pos = pb->sz;
		Error("PB: String buffer overflow!\n");
	}
}




static void create_directories(const char *path)
{
	const char *dir_begin = path, *dir_begin_next;
	char dir_path[PATH_MAX] = "";
	//size_t dir_len = 0;
	while((dir_begin_next = strchr(dir_begin, '/')))
	{
		dir_begin_next++;
		memcpy(&dir_path[0] + (dir_begin - path), dir_begin, dir_begin_next - dir_begin);
		//printf("mkdir %s\n",dir_path);
		mkdir(dir_path, 0777);
		dir_begin = dir_begin_next;
	}
}

#define MAX_RESP_SIZE 8192
static void serve_file(const char *path, int newsockfd, const char *mime, int binary)
{
	char resp[MAX_RESP_SIZE];
	printbuffer_t pb;
	int len;
	struct stat sb;
	int fd = open( path, O_RDONLY );
	const char *fname = strrchr(path, '/');

	stat( path, &sb );

	if(!fname) fname = path;
	else fname++;

	PB_Init( &pb, resp, sizeof( resp ));
	PB_PrintString( &pb, "HTTP/1.1 200 OK\r\n"
							"Server: webserver-c\r\n"
							"etag: %d-%d\r\n"
							"Content-Type: %s\r\n"
							"Content-Length: %d\r\n"
							"Accept-Ranges: bytes\r\n"
							"Date: Sat, 11 Nov 2023 21:55:54 GMT\r\n"
							"Content-Disposition : inline; filename=\"%s\"\r\n\r\n",
					(int)time(0), (int)sb.st_size, mime, (int)sb.st_size, fname );

	if( fd < 0 ) return;

	writeall(newsockfd, resp, pb.pos );

	while(( len = read(fd, resp, MAX_RESP_SIZE)) > 0)
	{
		int valwrite = writeall(newsockfd, resp, len);
		// Write to the socket
		if (valwrite < 0) {
			perror("webserver (write)");
			close(fd);
			return;
		}
	}
	close(fd);
}
static void serve_file_range( const char *path, int newsockfd, const char *mime, int start, int end )
{
	char resp[MAX_RESP_SIZE];
	printbuffer_t pb;
	int len, left = end - start + 1, rsize = MAX_RESP_SIZE;
	struct stat sb;
	int fd = open( path, O_RDONLY );
	const char *fname = strrchr(path, '/');

	lseek( fd, start, SEEK_SET );
	stat( path, &sb );

	if(!fname) fname = path;
	else fname++;

	PB_Init( &pb, resp, sizeof( resp ));
	PB_PrintString( &pb, "HTTP/1.1 206 Partial Content\r\n"
							"Server: webserver-c\r\n"
							"etag: %d-%d\r\n"
							"Content-Type: %s\r\n"
							"Content-Range: bytes %d-%d/%d\r\n"
							"Content-Length: %d\r\n"
							"Accept-Ranges: bytes\r\n"
							"Date: Sat, 11 Nov 2023 21:55:54 GMT\r\n"
							"Content-Disposition : inline; filename=\"%s\"\r\n\r\n",
					(int)time(0), (int)sb.st_size, mime,  start, end, (int)sb.st_size, left, fname );

	if( fd < 0 ) return;

	writeall(newsockfd, resp, pb.pos );
	if( rsize > left) rsize = left;

	while(( len = read(fd, resp, rsize)) > 0)
	{
		int valwrite = writeall(newsockfd, resp, len);
		// Write to the socket
		if (valwrite < 0) {
			perror("webserver (write)");
			close(fd);
			return;
		}
		left -= len;
		if( rsize > left) rsize = left;
		if(!left)
			break;
	}
	close(fd);
}

/* post request only operate in single-user mode and use last path form list/index */
static char post_filepath[1024];

static void serve_list(const char *path, int fd)
{
	PB_Declare( rd, MAX_RESP_SIZE );
	PB_Declare( rf, MAX_RESP_SIZE );
	char fpath[PATH_MAX] = {};
	int plen = strlen(path);
	DIR *dirp;

	if(!plen)
	{
		path = ".";
		plen = 1;
	}

	WriteStringLit(fd, "HTTP/1.1 200 OK\r\n"
		"Server: webserver-c\r\n"
		"Content-Type: text/plain\r\n\r\n[\n");
	printf("list %s\n", path);
	dirp = opendir(path);
	if (!dirp)
		return;
	if( plen > PATH_MAX - 2)
		plen = PATH_MAX - 2;
	strncpy( fpath, path, plen );
	strncpy( post_filepath, path, 1023 );
	fpath[plen++] = '/';


	while (1) {
		struct dirent *dp;
		struct stat sb;

		dp = readdir(dirp);
		if (!dp)
			break;
		if (dp->d_name[0] == '.')// && !dp->d_name[1])
			continue;
		strncpy(&fpath[plen], dp->d_name, PATH_MAX - plen - 1);
		if (stat(fpath, &sb) != 0)
			continue;
		printf("dir %s %d\n", dp->d_name, sb.st_mode);

		PB_PrintString(S_ISDIR(sb.st_mode)?&rd:&rf,"{\"name\": \"%s\", \"type\": %d, \"size\": %d},\n", dp->d_name, !S_ISDIR(sb.st_mode), (int)sb.st_size);
	}

	closedir(dirp);
	writeall(fd, rd_buffer, rd.pos);
	writeall(fd, rf_buffer, rf.pos);
	WriteStringLit(fd, "{\"name\": \"\", \"type\": -1, \"size\": 0}]");
}

static void serve_index(const char *path, int fd)
{
	PB_Declare( rd, MAX_RESP_SIZE );
	PB_Declare( rf, MAX_RESP_SIZE );
	char fpath[PATH_MAX] = {};
	int plen = strlen(path);
	DIR *dirp;

	if(!plen)
	{
		path = ".";
		plen = 1;
	}

	WriteStringLit(fd, "HTTP/1.1 200 OK\r\n"
					   "Server: webserver-c\r\n"
					   "Content-Type: text/html\r\n\r\n<html>"
					   "<body bgcolor=\"#606060\" text=\"#E0E0E0\" link=\"#F0F0D0\">"
					   "<table border=\"1\" width=\"100%\"><tr>"
					   "<td width=\"100%\">/files/");
	printf("list %s\n", path);
	dirp = opendir(path);
	if (!dirp)
		return;
	if( plen > PATH_MAX - 2)
		plen = PATH_MAX - 2;
	writeall(fd, path, plen);
	WriteStringLit(fd,"</td><td><a href=\"/index/\">/</a></td></tr>");
	strncpy(fpath, path, plen);
	strncpy( post_filepath, path, 1023 );
	fpath[plen++] = '/';

	while (1) {
		struct dirent *dp;
		struct stat sb;

		dp = readdir(dirp);
		if (!dp)
			break;
		if (dp->d_name[0] == '.')// && !dp->d_name[1])
			continue;
		strncpy(&fpath[plen], dp->d_name, PATH_MAX - plen - 1);
		if (stat(fpath, &sb) != 0)
			continue;
		printf("dir %s\n", dp->d_name);

		if( S_ISDIR( sb.st_mode ))
			PB_PrintString( &rd, "<tr><td width=\"100%\"><a href=\"/index/%s\">%s</a></td><td>(dir)</td></tr>", fpath, dp->d_name );
		else
			PB_PrintString( &rf, "<tr><td width=\"100%\"><a href=\"/files/%s\" target=\"_blank\">%s</a></td><td>%d</td></tr>", fpath, dp->d_name, (int)sb.st_size );
	}

	closedir(dirp);
	writeall(fd, rd_buffer, rd.pos);
	writeall(fd, rf_buffer, rf.pos);
	WriteStringLit(fd, "</table></body></html>");
}

static void serve_path_dav(const char *path, int fd);

static void serve_list_dav(const char *path, int fd)
{
	char resp[MAX_RESP_SIZE];
	char fpath[PATH_MAX] = {};
	const char *path2 = path;
	int plen = strlen(path);
	if(!plen)
	{
		path2 = ".";
		plen = 1;
	}
	//const char resp_list[] =
			DIR *dirp;
	printf("list %s\n", path2);
	dirp = opendir(path2);
	if (!dirp)
	{
		WriteStringLit( fd, "HTTP/1.1 404 Not found\r\n"
			"Server: webserver-c\r\n"
			"Content-Length: 0\r\n"
			"\r\n" );
		printf("bad dir %s %d\n", path, errno);
		return;

	}
	if( plen > PATH_MAX - 2)
		plen = PATH_MAX - 2;
	strncpy(fpath, path2, plen);
	/*len_dir += snprintf(
		&resp_dir[0] + len_dir, MAX_RESP_SIZE - len_dir,
		"<D:response><D:href>/files/%s</D:href><D:propstat><D:prop>"
		"<D:creationdate>Wed, 30 Oct 2019 18:58:08 GMT</D:creationdate>"
		"<D:getetag>\"01572461888\"</D:getetag>"
		"<D:getlastmodified>Wed, 30 Oct 2019 18:58:08 GMT</D:getlastmodified>"
		"<D:resourcetype><D:collection/></D:resourcetype>"
		"</D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response></D:multistatus>",
		fpath);*/
	if(fpath[plen - 1] != '/')
		fpath[plen++] = '/';


	int dirflag = 0;
	while (1) {
		struct dirent *dp;
		struct stat sb;

		dp = readdir(dirp);
		if (!dp)
		{
			if(!dirflag)
			{
				serve_path_dav(path, fd);
				return;
			}
			break;
		}
		if (dp->d_name[0] == '.' && (dp->d_name[1] == '.' || !dp->d_name[1]))// && !dp->d_name[1])
			continue;
		strncpy(&fpath[plen], dp->d_name, PATH_MAX - plen - 1);
		if (stat(fpath, &sb) != 0)
			continue;
		if(!dirflag)
		{
			WriteStringLit( fd, "HTTP/1.1 207 Multi-Status\r\n"
				"Server: webserver-c\r\n"
				"Content-Type: text/xml\r\n\r\n<?xml version=\"1.0\" encoding=\"utf-8\" ?><D:multistatus xmlns:D=\"DAV:\">" );
			{
				PB_Declare( resp1, 1024 );
				PB_PrintString( &resp1,
					"<D:response><D:href>/files/%s</D:href><D:propstat><D:prop>"
					"<D:creationdate>Wed, 30 Oct 2019 18:58:08 GMT</D:creationdate>"
					"<D:displayname></D:displayname>"
					//"<D:getetag>\"01572461888\"</D:getetag>"
					"<D:getlastmodified>Wed, 30 Oct 2019 18:58:08 GMT</D:getlastmodified>"
					"<D:resourcetype><D:collection/></D:resourcetype>"
					//"<d:quota-used-bytes>163</d:quota-used-bytes><d:quota-available-bytes>11802275840</d:quota-available-bytes>"
					"</D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response>",
					path2);
				if( path2[0] && (path2[0] != '.' || path2[1]) )
					writeall(fd, resp1_buffer, resp1.pos);
			}
			dirflag = 1;
		}
		printf("dir %s %s\n", fpath, dp->d_name);

		if(S_ISDIR(sb.st_mode))
		{
			PB_Declare( resp1, 1024);
			PB_PrintString( &resp1,
				"<D:response><D:href>/files/%s/</D:href><D:propstat><D:prop>"
				"<D:creationdate>Wed, 30 Oct 2019 18:58:08 GMT</D:creationdate>"
				"<D:displayname>%s</D:displayname>"
				//"<D:getetag>\"01572461888\"</D:getetag>"
				"<D:getlastmodified>Wed, 30 Oct 2019 18:58:08 GMT</D:getlastmodified>"
				"<D:resourcetype><D:collection/></D:resourcetype>"
				//"<d:quota-used-bytes>163</d:quota-used-bytes><d:quota-available-bytes>11802275840</d:quota-available-bytes>"
				"</D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response>",
				fpath, dp->d_name);
			writeall(fd, resp1_buffer, resp1.pos);
		}
		else if(1)
		{
			PB_Declare( resp1, 1024 );
			PB_PrintString( &resp1,
				"<D:response><D:href>/files/%s</D:href><D:propstat><D:prop>"
				"<D:creationdate>Wed, 30 Oct 2019 18:58:08 GMT</D:creationdate>"
				"<D:displayname>%s</D:displayname>"
				"<D:getcontentlength>%d</D:getcontentlength>"
				//"<D:getetag>\"01572461888\"</D:getetag>"
				"<D:getlastmodified>Wed, 30 Oct 2019 18:58:08 GMT</D:getlastmodified>"
				"<D:resourcetype />"
				//"<d:getcontenttype>text/plain</d:getcontenttype>"
				"</D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response>",
				fpath, dp->d_name, (int)sb.st_size);
			writeall(fd, resp1_buffer, resp1.pos);
		}

	}
	closedir(dirp);
	WriteStringLit(fd, "</D:multistatus>");
}

static void serve_path_dav(const char *path, int fd)
{
	const char *path2 = path;
	int plen = strlen(path);
	struct stat sb;
	PB_DeclareString( resp, MAX_RESP_SIZE, "HTTP/1.1 207 Multi-Status\r\n"
		"Server: webserver-c\r\n"
		"Content-Type: application/xml\r\n\r\n<?xml version=\"1.0\" encoding=\"utf-8\" ?><D:multistatus xmlns:D=\"DAV:\">");

	if(!plen)
	{
		path2 = ".";
		plen = 1;
	}
	int s = stat(path2, &sb);
/*	if(path[0] == '.')
	{
		const char resp_list1[] =
			"HTTP/1.1 207 OK\r\n"
			"Server: webserver-c\r\n"
			"Content-Type: application/xml\r\n\r\n<?xml version=\"1.0\" encoding=\"utf-8\" ?><D:multistatus xmlns:D=\"DAV:\" />";
		write(1, resp_list1, sizeof(resp_list1) - 1);
		writeall(fd, resp_list1, sizeof(resp_list1) - 1);
		return;
	}*/
	if(s) // || (!S_ISDIR(sb.st_mode) && sb.st_size == 0))
	{
		WriteStringLit( fd, "HTTP/1.1 404 Not found\r\n"
			"Server: webserver-c\r\n"
			"Content-Type: application/xml\r\n\r\n<?xml version=\"1.0\" encoding=\"utf-8\" ?><D:multistatus xmlns:D=\"DAV:\" />");
		printf("bad stat %s %d %d %d\n", path, s, errno, sb.st_mode);
		return;

	}

	PB_PrintString( &resp,
		"<D:response><D:href>/files/%s</D:href><D:propstat><D:prop>"
		"<D:creationdate>Wed, 30 Oct 2019 18:58:08 GMT</D:creationdate>"
		"<D:getcontentlength>%d</D:getcontentlength>"
		"<D:getetag>\"01572461888\"</D:getetag>"
		"<D:getlastmodified>Wed, 30 Oct 2019 18:58:08 GMT</D:getlastmodified>"
		"%s"
		"</D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response></D:multistatus>",
		path, (!S_ISDIR(sb.st_mode))?(int)sb.st_size:0,
		 S_ISDIR(sb.st_mode)?"<D:resourcetype><D:collection/></D:resourcetype>":"<D:resourcetype /><d:getcontenttype>text/plain</d:getcontenttype>");
	//printf("clen %d\n", (int)(strlen("<?xml version=\"1.0\" encoding=\"utf-8\" ?><D:multistatus xmlns:D=\"DAV:\">") + len_dir));
	//write(1, resp_dir, len_dir);
	writeall( fd, resp_buffer, resp.pos );
}
#ifdef ENABLE_SUNZIP
#include "sunzip/sunzip_integration.h"
static char sunzip_root[1024];
static char *sunzip_root_end;
static size_t sunzip_len, sunzip_pos, sunzip_extralen;
struct printbuffer_s sunzip_printb;
static char sunzip_output[4096];

void sunzip_fatal( void )
{
	RB_Skip( sunzip_len + sunzip_extralen - sunzip_pos );
	PB_PrintString( &sunzip_printb, "sunzip: fatal after %d of %d bytes\n", (int)sunzip_pos, (int)sunzip_len );
	writeall( rbstate.fd, sunzip_output, sunzip_printb.pos );
	close(rbstate.fd);
	puts(sunzip_output);
	_exit(1);
}

int sunzip_read(sunzip_file_in file, void *buffer, size_t size)
{
	int ret;
	if( sunzip_pos + size > sunzip_len )
		size = sunzip_len - sunzip_pos;
	if( size == 0 )
		return 0;
	printf("read %d\n", size);
	ret = RB_Read(buffer, size);
	if( ret > 0)
		sunzip_pos += ret;
	return ret;
}
sunzip_file_out sunzip_openout(const char *filename)
{
	S_strncpy( sunzip_root_end, filename, &sunzip_root[1023] - sunzip_root_end );
	create_directories( sunzip_root );
	printf( "%s\n", sunzip_root );
	return open( sunzip_root, O_WRONLY | O_CREAT, 0777 );
}
#endif
static void SV_PutZip(int fd, const char *path, int clen )
{
#ifdef ENABLE_SUNZIP
	while(path[0] == '/')path++;
	sunzip_root_end = &sunzip_root[S_strncpy( sunzip_root, path, 1023 )];
	sunzip_len = clen;
	sunzip_pos = sunzip_extralen = 0;
	PB_Init( &sunzip_printb, sunzip_output, 4096 );

	PB_WriteStringLit( &sunzip_printb, "HTTP/1.1 200 OK\r\n"
									  "Server: webserver-c\r\n"
									  "Content-type: text/html\r\n\r\n"
									   );
	sunzip( 0, 1 );

	writeall( fd, sunzip_output, sunzip_printb.pos );
#endif
}
#define htoi(x) (9 * (x >> 6) + (x & 017))
static unsigned int S_htoi(const char *s) {
#if 0
    unsigned int acum = 0;
    char c;;

    while(((c = *s) >= '0')&&(c <= '9') ||(c >= 'A')&&(c <= 'F')) {
      if(c > 57) c-= 8;
      acum = acum << 4;
      acum = acum + (c - 48);
      s++;
    }
    return acum;
#else
	unsigned int acum = 0;

	while(*s >= '0')
	{
		acum <<= 4;
		acum += htoi(*s);
		s++;
	}
	return acum;
#endif
}


static void SV_Put(int newsockfd, const char *uri, int clen )
{
	PB_DeclareString(resp_ok, 1024,"HTTP/1.1 201 Created\r\n"
									"Server: webserver-c\r\n"
									"Location: /files/");
	int fd;
	const char *path = uri;
	if(!strncmp(path, "/zip/", 4))
	{
		SV_PutZip( newsockfd, uri + 4, clen );
		return;
	}
	if(strncmp(path, "/files/", 7) || strstr(path, ".."))
#ifdef ENABLE_FORK
		_exit(1);
#else
		return;
#endif

	path += 7;
	while(path[0] == '/')path++;
	create_directories(path);
	fd = open(path, O_CREAT | O_WRONLY, 0666);
	int ret = RB_Dump( fd, clen );
	printf("done %s\n", path);
	if(ret > 0)
		ftruncate(fd,ret);
	close(fd);

	if( ret >= 0)
	{
		PB_WriteString( &resp_ok, path );
		PB_WriteStringLit( &resp_ok, "\r\nContent-type: text/html\r\n\r\n"
						  "OK");
		writeall( newsockfd, resp_ok_buffer, resp_ok.pos );
	}
}

static void SV_PutChunked( int newsockfd, const char *uri, int explen )
{
	PB_DeclareString(resp_ok, 1024,"HTTP/1.1 201 Created\r\n"
									"Server: webserver-c\r\n"
									"Location: /files/");
	int fd;
	const char *path = uri;
	char chunkstr[16];
	unsigned int chunklen;
	size_t filelen = 0;

	if(strncmp(path, "/files/", 7) || strstr(path, ".."))
		_exit(1);

	path += 7;
	while(path[0] == '/')path++;
	create_directories(path);
	fd = open(path, O_CREAT | O_WRONLY, 0666);
	do
	{
		int ret;
		if( RB_ReadLine( chunkstr, 15 ) <= 0 )
			break;
		printf("chunk hex %s\n", chunkstr);

		chunklen = S_htoi( chunkstr );

		printf("chunk len %d\n", chunklen);
		if(!chunklen)
			break;

		ret = RB_Dump( fd, chunklen );
		if( ret < 0)
			break;
		filelen += ret;
		if( explen && filelen == explen )
			break;
		RB_SkipLine();
		//RB_Dump(1, 2);


	}while(1);

	printf( "done %s %d\n", path, (int)filelen );

	if(filelen > 0)
		ftruncate(fd,filelen);
	close(fd);

	PB_WriteString( &resp_ok, path );
	PB_WriteStringLit( &resp_ok, "\r\nContent-type: text/html\r\n\r\n"
					  "OK");
	writeall( newsockfd, resp_ok_buffer, resp_ok.pos );
}

static void SV_PostUpload(int fd, const char *uri, int clen, const char *boundary, int boundary_len )
{
	char line[1024];
	char filename[1024] = "upload_file";
	PB_Declare( filepath, 1024);
	int skiplen = 0, dumpfd;
	PB_WriteString( &filepath, post_filepath );
	PB_WriteStringLit( &filepath, "/" );
	if( !post_filepath[0] )
		return;

	while( line[0] != '\r' )
	{
		skiplen += RB_ReadLine(line, 1024) + 2;
		if(!strncasecmp( line, "Content-Disposition: form-data; name=\"file\"; filename=\"", sizeof("Content-Disposition: form-data; name=\"file\"; filename=")))
		{
			char *end;

			if( !strstr( line, ".." ))
			strcpy(filename, &line[0] + sizeof( "Content-Disposition: form-data; name=\"file\"; filename=" ));
			if(( end = strchr(filename, '\"')))
				*end = 0;
			printf("filename %s\n", filename);
		}
		puts(line);
	}
	PB_WriteString( &filepath, filename );
	puts( filepath_buffer );
#if 0
	if( !strcmp( uri, "/legacyzip" ))
	{
		sunzip_root_end = &sunzip_root[S_strncpy( sunzip_root, post_filepath, 1023 )];
		*sunzip_root_end++ = '/';
		sunzip_len = clen - skiplen - boundary_len ;
		sunzip_pos = 0;
		sunzip_extralen = boundary_len + 8;
		PB_Init( &sunzip_printb, sunzip_output, 4096 );

		PB_WriteStringLit( &sunzip_printb, "HTTP/1.1 200 OK\r\n"
										  "Server: webserver-c\r\n"
										  "Content-type: text/html\r\n\r\n"
						  );
		sunzip( 0, 1 );
		RB_Skip( boundary_len + 8 );
		printf("done\n");

		writeall( fd, sunzip_output, sunzip_printb.pos );
	}
	else
#endif
	{
		dumpfd = open( filepath_buffer, O_WRONLY | O_CREAT, 0755 );
		//write(1, "beg\n", 4);
		RB_Dump( dumpfd, clen - skiplen - boundary_len );
		//write(1, "end\n", 4);
		close( dumpfd );
		RB_Dump( 1, boundary_len + 8 );
		//RB_SkipLine();
		WriteStringLit( fd, "HTTP/1.1 200 OK\r\n"
								  "Server: webserver-c\r\n"
								  "Content-type: text/html\r\n\r\n"
								  "OK" );
	}
}

#include "zipflow/zipflow.h"
#if 0

void SV_ZipFlow( ZIP *zip, const char *path, int fd )
{
	char fpath[PATH_MAX] = {};
	int plen = strlen(path);
	DIR *dirp;

	if(!plen)
	{
		path = ".";
		plen = 1;
	}

	printf("zip %s\n", path);
	dirp = opendir(path);
	if (!dirp)
		return;
	if( plen > PATH_MAX - 2)
		plen = PATH_MAX - 2;
	strncpy( fpath, path, plen );
	fpath[plen++] = '/';


	while (1) {
		struct dirent *dp;
		struct stat sb;

		dp = readdir(dirp);
		if (!dp)
			break;
		if (dp->d_name[0] == '.')// && !dp->d_name[1])
			continue;
		strncpy(&fpath[plen], dp->d_name, PATH_MAX - plen - 1);
		if (stat(fpath, &sb) != 0)
			continue;

		if( S_ISDIR(sb.st_mode) )
		{
			printf("zipdir %s\n", dp->d_name);
			SV_ZipFlow( zip, fpath, fd );
		}
		else
		{
			printf("zipfile %s\n", dp->d_name);
			zip_entry( zip, fpath );
		}
	}

	closedir(dirp);
}
#endif

static int zflow_write(void *fd, const void *ptr, size_t len)
{
//	return writeall((int)fd,ptr,len) != len;
	return writeall(*(int*)fd,ptr,len) != len;
}

int main(int argc, char **argv, char **envp) {
	char buffer[BUFFER_SIZE] = { };
	int sockfd;
	unsigned short port = PORT;

	//printf("%d %p %p\n", argc, argv, envp);
	if(argc == 3)
	{
		chdir(argv[1]);
		port = atoi(argv[2]);
	}

	// Create a socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) {
		perror("webserver (socket)");
		return 1;
	}
	post_filepath[0] = '.';
	printf("socket created successfully\n");
	const int enable = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
		perror("setsockopt(SO_REUSEADDR) failed");
	// Create the address to bind the socket to
	struct sockaddr_in host_addr;
	int host_addrlen = sizeof(host_addr);

	host_addr.sin_family = AF_INET;
	host_addr.sin_port = htons(port);
	host_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	// Create client address
	struct sockaddr_in client_addr;
	int client_addrlen = sizeof(client_addr);

	// Bind the socket to the address
	if (bind(sockfd, (struct sockaddr *)&host_addr, host_addrlen) != 0) {
		perror("webserver (bind)");
		return 1;
	}
	printf("socket successfully bound to address\n");

	// Listen for incoming connections
	if (listen(sockfd, SOMAXCONN) != 0) {
		perror("webserver (listen)");
		return 1;
	}
	printf("server listening for connections: http://localhost:%d\n",PORT);

	for (;;) {
		// Accept incoming connections
		int newsockfd = accept(sockfd, (struct sockaddr *)&host_addr,
							   (socklen_t *)&host_addrlen);
		if (newsockfd < 0) {
			perror("webserver (accept)");
			continue;
		}
		printf("connection accepted\n");

		// Get client address
		int sockn = getsockname(newsockfd, (struct sockaddr *)&client_addr,
								(socklen_t *)&client_addrlen);
		if (sockn < 0) {
			perror("webserver (getsockname)");
			close(newsockfd);
			continue;
		}

		// Read the request
		char method[METHOD_LEN] = "", uri[URI_LEN] = "";
		RB_Init(newsockfd);
		if( RB_ReadHeaders( method, uri, buffer, sizeof( buffer ) - 1) < 0 )
		{
			close( newsockfd );
			continue;
		}

		const char *contentlength = strcasestr(buffer, "content-length: ");
		int clen = 0;
		if(contentlength)
		{
			Report("content-length %s\n", contentlength);
			clen = atoi(contentlength + sizeof("content-length: ") - 1);
		}

		printf("[%s:%u] %s %s\n", inet_ntoa(client_addr.sin_addr),
			   ntohs(client_addr.sin_port), method, uri);


		if(!strcmp(method,"PUT"))
		{
			int r = 0;
#ifdef ENABLE_FORK
			r = fork();
#endif
			if(r == 0) // child, copy the file
			{
				puts( buffer );
				if( clen > 0 )
					SV_Put( newsockfd, uri, clen );
				else
				{
					// Apple like to send some chunks
					if( strcasestr( buffer, "transfer-encoding: chunked" ))
					{
						int explen = 0;
						char *el = strcasestr( buffer, "x-expected-entity-length: " );
						if(el)
							explen = atoi( el + sizeof( "x-expected-entity-length:" ));
						SV_PutChunked( newsockfd, uri, explen );
					}
					else
						SV_Put( newsockfd, uri, 0 );
				}
				close(newsockfd);
#ifdef ENABLE_FORK
				_exit(0);
#endif
			}
			else
			{
				close(newsockfd);
				if(r < 0)
				{
					perror("fork");
					close(sockfd);
					return 1;
				}
			}
		}
		else if(!strcmp(method,"POST"))
		{
			int r = 0;
#ifdef ENABLE_FORK
			r = fork();
#endif
			if(r == 0) // child, copy the file
			{
				char *boundary = strcasestr( buffer, "content-type: multipart/form-data; boundary=" );
				puts( buffer );
				if(boundary)
				{
					char *boundary_end = strchr( boundary, '\r');
					boundary += sizeof("content-type: multipart/form-data; boundary");
					if(!boundary_end)_exit(1);
					int boundary_len = boundary_end - boundary;
					SV_PostUpload( newsockfd, uri, clen, boundary, boundary_len );
				}


				close(newsockfd);
#ifdef ENABLE_FORK
				_exit(0);
#endif
			}
			else
			{
				close(newsockfd);
				if(r < 0)
				{
					perror("fork");
					close(sockfd);
					return 1;
				}
			}
		}
		else if(!strcmp(method, "DELETE"))
		{
			char *path = uri;
			if(strstr(path, ".."))
			{
				close(newsockfd);
				continue;
			}
			if(!strncmp(path, "/files/", 7))
			{
				path += 7;
				unlink(path);
				WriteStringLit(newsockfd, "HTTP/1.1 200 OK\r\n"
									   "Server: webserver-c\r\n"
									   "Content-type: text/html\r\n\r\n"
							   "OK");
			}
			close(newsockfd);
		}
		else if(!strcmp(method, "GET"))
		{
			char *path = uri;
			if(strstr(path, ".."))
			{
				close(newsockfd);
				continue;
			}
			if(!strncmp(path, "/list/", 6))
			{
				path += 6;
				serve_list(path, newsockfd);
			}
			else if(!strncmp(path, "/zip/", 5))
			{
#ifdef ENABLE_ZIPFLOW
//				ZIP *zip = zip_pipe( (void*)newsockfd, zflow_write, 1 );
				ZIP *zip = zip_pipe( (void*)&newsockfd, zflow_write, 1 );
                char *p;
				path += 5;
				p = strrchr( path, '.' );
				if(p)*p = 0;


				WriteStringLit( newsockfd, "HTTP/1.1 200 OK\r\n"
								   "Server: webserver-c\r\n"
								   "Content-Type: application/x-zip-compressed\r\n"
								   "Content-Disposition : attachment; filename=\"folder.zip\"\r\n\r\n" );

				//SV_ZipFlow( zip, path, newsockfd );
				zip_entry( zip, path );
				zip_close( zip );
				//fclose(f);//XXX what is?
#endif
			}
			else if(!strcmp(path, "/indexredir"))
			{
				WriteStringLit(newsockfd, "HTTP/1.1 200 OK\r\n"
										  "Server: webserver-c\r\n"
										  "Content-type: text/html\r\n\r\n"
										  "<html><body bgcolor=\"#000000\" link=\"#F0F0D0\"><a href=\"/index/\">list files</a></body></html>");
			}
			else if(!strncmp(path, "/index/", 7))
			{
				path += 7;
				serve_index(path, newsockfd);
			}
			else if(strncmp(path, "/files/", 7))
			{
#ifdef ENABLE_PAGE
				WriteStringLit(newsockfd, "HTTP/1.1 200 OK\r\n"
										  "Server: webserver-c\r\n"
										  "Content-type: text/html\r\n\r\n");
				WriteStringLit(newsockfd, page_content);
#else
				serve_file("folderupload.html", newsockfd, "text/html", 1);
#endif
			}
			else
			{
				int r = 0;
#ifdef ENABLE_FORK
				r =fork();
#endif
				if( r == 0 )
				{
					char *rng = strcasestr( buffer, "\nrange: bytes=" );
					path += 7;
					puts(buffer);
					if( rng )
					{
						char *rng1;
						rng += sizeof( "\nrange: bytes" );
						rng1 = strchr(rng, '-');
						if(rng1)
							serve_file_range(path, newsockfd, "application/octet-stream", atoi(rng), atoi(rng1));
					}
					else
						serve_file(path, newsockfd, "application/octet-stream", 1);
				}
				else
				{
					close(newsockfd);
					if(r < 0)
					{
							perror("fork");
							close(sockfd);
							return 1;
					}
				}
			}

			close(newsockfd);
		}
		else if(!strcmp(method, "HEAD"))
		{
			char *path = uri;

			//usleep(15000);

			RB_Dump( 1, clen );

			if(strncmp(path, "/files/", 7) || strstr(path, ".."))
			{
				close(newsockfd);
				continue;
			}
			path += 7;
			struct stat sb;
				if(!stat(path,&sb))
				{
					const char *fname = strrchr(path, '/');
					printbuffer_t resp;
					if(!fname) fname = path;
					else fname++;

					PB_Init( &resp, buffer, sizeof( buffer ) - 1);
					PB_PrintString( &resp,
								   "HTTP/1.1 200 OK\r\n"
								   "Server: webserver-c\r\n"
								   "etag: %d-%d\r\n"
								   "Content-Type: %s\r\n"
								   "Content-Length: %d\r\n"
								   "Accept-Ranges: bytes\r\n"
								   "Date: Sat, 11 Nov 2023 21:55:54 GMT\r\n"
								   "Content-Disposition : inline; filename=\"%s\"\r\n\r\n", (int)time(0), (int)sb.st_size, "text/plain", (int)sb.st_size, fname );
					writeall( newsockfd, buffer, resp.pos );
					printf("HEAD %s %s %d\n", path, fname, (int)sb.st_size);
				}
				else
					WriteStringLit(newsockfd, "HTTP/1.1 404 Not found\r\n"
											  "Server: webserver-c\r\n"
											  "Content-Length:0\r\n"
											  "\r\n");
			close(newsockfd);
		}
		else if(!strcmp(method, "MKCOL"))
		{
			char *path = uri;
			if(strncmp(path, "/files/", 7) || strstr(path, ".."))
			{
				close(newsockfd);
				continue;
			}
			path += 7;
			create_directories(path);
			mkdir(path, 0777);
			puts(buffer);
			//usleep(10000);

			RB_Dump(1, clen);
			WriteStringLit(newsockfd,"HTTP/1.1 201 Created\r\n"
									  "Server: webserver-c\r\n\r\n" )
			close(newsockfd);
		}
		else if(!strcmp(method, "PROPPATCH"))
		{
			char *path = uri;
			PB_DeclareString( resp_ok, 1024,  "HTTP/1.1 207 Multi-Status\r\n"
											"Server: webserver-c\r\n"
											"Content-type: application/xml\r\n\r\n"
											"<?xml version=\"1.0\" encoding=\"utf-8\" ?><D:multistatus xmlns:D=\"DAV:\"><D:response><D:href>" );
			if(strncmp(path, "/files/", 7) || strstr(path, ".."))
			{
				close(newsockfd);
				continue;
			}
			printf("%s\n", buffer);
			RB_Dump(1, clen);
			PB_WriteString( &resp_ok, uri );
			PB_WriteStringLit( &resp_ok, "</D:href><D:propstat><D:prop></D:prop><D:status>HTTP/1.1 403 Forbidden</D:status></D:propstat></D:response></D:multistatus>");
			writeall(newsockfd, resp_ok_buffer, resp_ok.pos );

			close(newsockfd);
		}
		else if(!strcmp(method, "MOVE"))
		{
			char *path = uri;
			const char *dest;
			if(strncmp(path, "/files/", 7) || strstr(path, ".."))
			{
				close(newsockfd);
				continue;
			}
			path += 7;

			RB_Dump(1, clen);
			dest = strcasestr(buffer, "destination: ");
			if(dest)
			{
				char *e1 = strstr(dest, "\n");
				char *e2 = strstr(dest, "\r\n");
				if(e2 && e2 < e1)
					e1 = e2;
				*e1 = 0;
				dest += sizeof("destination: " ) - 1;
				printf("move %s %s\n", path, dest);
				if(!strncmp(dest, "/files/", 7) && !strstr(dest, ".."))
				{
					dest += 7;
					rename(path, dest);
				}
			}

			WriteStringLit(newsockfd,  "HTTP/1.1 201 Created\r\n"
										  "Server: webserver-c\r\n"
										  "Content-type: text/html\r\n\r\n"
										  "OK");
			close(newsockfd);
		}
		else if(!strcmp(method, "PROPFIND"))
		{
			char *path = uri;
			const char resp_auth[] = "HTTP/1.1 401 Unauthorized\r\n"
								   "Server: webserver-c\r\n"
								   "WWW-Authenticate: Basic realm=\"User Visible Realm\"\r\n\r\n";
			RB_Dump(1, clen);
			/*if(!strcasestr(buffer, "authorization: "))
			{
				writeall(newsockfd, resp_auth, sizeof(resp_auth) - 1);
				close(newsockfd);
			}*/
            if(path[0]=='/' && path[1] == '\0')
            {
               path="/files";
            }
			if(strncmp(path, "/files", 6) || strstr(path, ".."))
			{
				serve_path_dav("", newsockfd);
				close(newsockfd);
				continue;
			}
			path += 7;
			printf("%s\n", buffer);
			if(strcasestr( buffer, "Depth: 0" ))
			{
				serve_path_dav(path, newsockfd);
			}
			else
			{
				serve_list_dav(path, newsockfd);
			}

			close(newsockfd);
		}
		else if(!strcmp(method, "OPTIONS"))
		{
			/*"HTTP/1.1 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Access-Control-Allow-Methods: PROPFIND, PROPPATCH, COPY, MOVE, DELETE, MKCOL, PUT, UNLOCK, GETLIB, VERSION-CONTROL, CHECKIN, CHECKOUT, UNCHECKOUT, REPORT, UPDATE, CANCELUPLOAD, HEAD, OPTIONS, GET, POST\r\n"
			"Access-Control-Allow-Headers: Overwrite, Destination, Content-Type, Depth, User-Agent, X-File-Size, X-Requested-With, If-Modified-Since, X-File-Name, Cache-Control\r\n"
			"Access-Control-Max-Age: 86400\r\n\r\n";*/
			//if( valread >= 0)
			RB_Dump(1, clen);
			WriteStringLit(newsockfd, "HTTP/1.1 200 OK\r\nAllow: GET,HEAD,PUT,OPTIONS,DELETE,PROPFIND,COPY,MOVE\r\nDAV: 1,2\r\nContent-Length: 0\r\n\r\n");
			close(newsockfd);
		}
		else if(!strcmp(method, "LOCK"))
		{
			char lock_headers[512];
			char lock_body[1024];
			printbuffer_t lb, lh;
			static int count;
#if 0
			char lock_token [] = "opaquelocktoken:7028f329-f1cf-4123-a34c-dba594689257";
			lock_token[17] += count++ % 10;
#else
			char lock_token [32] = "";
			snprintf(lock_token, 31, "%d", (int)time(0));//(int)count++);//time(0));
			//lock_token[1] += count++ % 10;
#endif
			//usleep(5000);
			RB_Dump(1, clen);

			PB_Init( &lb, lock_body, 1024 );
			PB_Init( &lh, lock_headers, 512 );
			PB_PrintString( &lb,
			"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
			"<D:prop xmlns:D=\"DAV:\"><D:lockdiscovery><D:activelock>"
			"<D:locktoken><D:href>%s</D:href></D:locktoken>"
			"<D:lockroot><D:href>%s</D:href></D:lockroot>"
			"</D:activelock></D:lockdiscovery></D:prop>", lock_token, uri );
			PB_PrintString( &lh, "HTTP/1.1 200 OK\r\n"
			"Content-Type: application/xml; charset=utf-8\r\n"
			"Lock-Token: <%s>\r\n"
			"Content-Length: %d\r\n"
			"Date: Fri, 10 Nov 2023 23:45:40 GMT\r\n\r\n", lock_token, lb.pos );
			writeall(newsockfd, lock_headers, lh.pos);
			writeall(newsockfd, lock_body, lb.pos);
			close(newsockfd);
		}
		else if(!strcmp(method, "UNLOCK"))
		{
//			writeall(newsockfd, resp_ok, strlen(resp_ok));
			WriteStringLit(newsockfd, "HTTP/1.1 200 OK\r\n"
									  "Server: webserver-c\r\n"
									  "Content-type: application/xml\r\n\r\n");
			close(newsockfd);

		}
		else
		{
			// todo: answer 4XX
			close(newsockfd);
		}
	}

	close(sockfd);
	return 0;
}
/*
#define OF(x) x
#ifdef ENABLE_SUNZIP
//#include "sunzip/sunzip.c"
   #undef local
   #undef CHUNK
   //#include "zlib/infback.c"
   #undef PULLBYTE
   //#include "zlib/inffast.c"
   //#include "zlib/inflate.c"
   //#include "zlib/inftrees.c"
#endif

#ifdef ENABLE_ZIPFLOW
   //#undef local
   #define warn(f,...) printf(f"\n",__VA_ARGS__)
#endif
*/
