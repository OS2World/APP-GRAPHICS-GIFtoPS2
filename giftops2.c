/*********************************************
 *             GIFtoPS Converter             *
 *                                           *
 *      May 16, 1988  by Scott Hemphill      *
 *                                           *
 * I wrote this program, and hereby place it *
 * in the public domain, i.e. there are no   *
 * copying restrictions of any kind.         *
 *					     *
 * Command line processing, EPSF support,    *
 * and image positioning by Mic Kaczmarczik, *
 * modeled after James Frew's ``suntops''    *
 *					     *
 * All ints were assumed 32 bits, changed to *
 * work with 16 bit ints with mwc on Atari ST*
 * The routine below provides the getopt()   *
 * routine unavailable with mwc.             *
 * A GEM version for the Atari ST may be     *
 * appearing soon in the Atari SIG.          *
 * Mark Storkamp 3/24/90.		     *
 *                                           *
 * Converted to compile under Turbo C 2.0    *
 * Changed "raster","fill", and "scanline"   *
 * to HUGE pointers and compiled using       *
 * "compact" memory model. Also stripped out *
 * the getopt() function (for troubleshooting*
 * purposes). Turbo C includes getopt() if   *
 * you want it. Atof() were substituted for  *
 * sscanf() in the command line parser (for  *
 * getting imageheight and imagewidth values,*
 * I was unable to get atof to work with my  *
 * compiler).                                *
 * Craig Moore 4/13/90                       *
 *                                           *
 * Converted to compile under Borland C++ for*
 * OS/2 2.xx 32 bit                          *
 * Function parameters declaration converted *
 * from K&R style to ANSI.                   *
 * Definition of type UCHAR for UNSIGNED     *
 * CHAR, UINT for UNSIGNED INT, and ULONG for*
 * UNSIGNED LONG (also if ULONG and UINT have*
 * the same size under this compiler).       *
 * Stripped HUGE pointer, not necessary under*
 * OS/2 2.xx because of flat memory model.   *
 * Stripped register var declaration because *
 * BORLAND compiler for OS/2 ignores the     *
 * declaration putting vars in register as   *
 * often as possible.                        *
 * Modified functions:                       *
 * 	readimage()                          *
 * Stripped farmalloc and farfree, because of*
 * flat memory model.                        *
 * Stripped left var because was not used.   *
 * 	writeheader()                        *
 * Stripped parameters UINT left and UINT top*
 * because they are not used.                *
 * 	readextension()                      *
 * count type converted from UCHAR to int.   *
 * modified while loop to shut a worning out.*
 * 	main()                               *
 * Added check for no args.                  *
 * Original program crashes with no argument *
 * because it try to make an attempt to      *
 * strcpy(gifname, \0) !!!!                  *
 * Fabrizio Fioravanti 23/8/93               *
 * Maurizio Giunti     23/8/93               *
 *********************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <alloc.h>


#define FALSE 0
#define TRUE 1

typedef int bool;
typedef unsigned char UCHAR;
typedef unsigned int UINT;
typedef unsigned long ULONG;

typedef struct codestruct {
	    struct codestruct *prefix;
	    unsigned char first,suffix;
	} codetype;


FILE *infile;
UINT screenwidth;           /* The dimensions of the screen */
UINT screenheight;          /*   (not those of the image)   */
bool global;                        /* Is there a global color map? */
int globalbits;                     /* Number of bits of global colors */
UCHAR globalmap[256][3];    /* RGB values for global color map */
char colortable[256][3];            /* Hex intensity strings for an image */
UCHAR *raster;              /* Decoded image data */
codetype codetable[4096];                /* LZW compression code data */
int datasize,codesize,codemask;     /* Decoder working variables */
int clear,eoi;                      /* Special code values */

#define INCH  72.0
int EPSF = FALSE;		    /* should we generate EPSF?		*/
int screen = FALSE;	            /* use alternate halftone screen?	*/
int landscape = FALSE;		    /* display image in landscape mode? */
int copies = 1;			    /* number of copies to create	*/

double pagewidth;
double pageheight;
double imagewidth;
double imageheight;


void usage(void)
{
    fprintf(stderr,"usage: GIFtoPS2 [-els] [-c{copies}] [-w{width}] [-h{height}] [file]\n");
    exit(-1);
}

void fatal(char *s)
{
	fprintf(stderr,"giftops: %s\n",s);
	exit(-1);
}

void checksignature(void)
{
	char buf[6];

	fread(buf,1,6,infile);
	if (strncmp(buf,"GIF",3)) fatal("file is not a GIF file");
	if (strncmp(&buf[3],"87a",3)) fatal("unknown GIF version number");
}

/* Get information which is global to all the images stored in the file */

void readscreen(void)
{
	UCHAR buf[7];

	fread(buf,1,7,infile);
	screenwidth = buf[0] + (buf[1] << 8);
	screenheight = buf[2] + (buf[3] << 8);
	global = buf[4] & 0x80;
	if (global) {
	    globalbits = (buf[4] & 0x07) + 1;
	    fread(globalmap,3,1<<globalbits,infile);
	}
}

/* Convert a color map (local or global) to an array of two character
   hexadecimal strings, stored in colortable.  RGB is converted to
   8-bit grayscale using integer arithmetic. */

void initcolors(char colortable[256][3],UCHAR colormap[256][3],int ncolors)
{
	static char hextab[] = {'0','1','2','3','4','5','6','7',
				'8','9','A','B','C','D','E','F'};
	UINT color;
	int i;

	for (i = 0; i < ncolors; i++) {
	    color = 77*colormap[i][0] + 150*colormap[i][1] + 29*colormap[i][2];
	    color >>= 8;
	    colortable[i][0] = hextab[color >> 4];
	    colortable[i][1] = hextab[color & 15];
	    colortable[i][2] = '\0';
	}
}

/* Write a postscript header to the standard output.
 *
 * imagewidth and imageheight always represent the width/height of the
 * bounding box in the standard PostScript coordinate system, even when
 * the image rotated 90 degrees.
 */

void writeheader(UINT width,UINT height)
{
	double aspect, xorg, yorg;

	/*
	 * using imagewidth and imageheight as maxima, figure out how
	 * tall and wide the image will be on the PostScript page.
	 */
	aspect = ((double) width) / ((double) height);
	if (landscape) {
		if (aspect >= imageheight / imagewidth)
			imagewidth = imageheight / aspect;
		else
			imageheight = imagewidth * aspect;
	} else {
		if (aspect >= imagewidth / imageheight)
			imageheight = imagewidth / aspect;
		else
			imagewidth = imageheight * aspect;
	}

	/*
	 * For simplicity's sake, EPSF files have origin (0, 0).
	 */
	if (EPSF) {
		xorg = yorg = 0.0;
		printf("%%!PS-Adobe-2.0 EPSF-1.2\n");	/* magic number */
	} else {
		xorg = (pagewidth - imagewidth) / 2.0;
		yorg = (pageheight - imageheight) / 2.0;
		printf("%%!\n");
	}

	printf("%%%%BoundingBox: %.3f %.3f %.3f %.3f\n",
	       xorg, yorg, xorg + imagewidth, yorg + imageheight);
	printf("%%%%Creator: GIFtoPS2\n");
	printf("%%%%EndComments\n");

	/*
	 * setting # of copies doesn't make sense if creating EPSF file
	 */
	if (!EPSF)
		printf("/#copies %d def\n", copies);
	printf("gsave\n");
	printf("/alternatehalftone %s def\n", screen ? "true" : "false");
	printf("alternatehalftone {\n");
	printf("   currentscreen\n");
	printf("   /proc exch def /angle exch def /frequency exch def\n");
	printf("   /angle 90 def /frequency 60 def\n");
	printf("   frequency angle /proc load setscreen\n");
	printf("} if\n");

	printf("/picstr %d string def\n",width);
	printf("/screen {\n");
	printf("   %d %d 8 [%d 0 0 -%d 0 %d]\n",
	       width, height, width, height, height);
	printf("   {currentfile picstr readhexstring pop} image} def\n");
	printf("%.3f %.3f translate\n", xorg, yorg);
	if (landscape) {
		printf("%.3f 0 translate 90 rotate %.3f %.3f scale\n",
		       imagewidth, imageheight, imagewidth);
	} else {
		printf("%.3f %.3f scale\n", imagewidth, imageheight);
	}
	printf("screen\n");
}


/* Output the bytes associated with a code to the raster array */

void outcode(codetype *p,UCHAR **fill)
{
	if (p->prefix) outcode(p->prefix,fill);
	*(*fill)++ = p->suffix;
}

/* Process a compression code.  "clear" resets the code table.  Otherwise
   make a new code table entry, and output the bytes associated with the
   code. */

void process(int code,UCHAR **fill)
{
	static avail,oldcode;
	codetype *p;

	if (code == clear) {
	    codesize = datasize + 1;
	    codemask = (1 << codesize) - 1;
	    avail = clear + 2;
	    oldcode = -1;
	} else if (code < avail) {
	    outcode(&codetable[code],fill);
	    if (oldcode != -1) {
		p = &codetable[avail++];
		p->prefix = &codetable[oldcode];
		p->first = p->prefix->first;
		p->suffix = codetable[code].first;
		if ((avail & codemask) == 0 && avail < 4096) {
		    codesize++;
		    codemask += avail;
		}
	    }
	    oldcode = code;
	} else if (code == avail && oldcode != -1) {
	    p = &codetable[avail++];
	    p->prefix = &codetable[oldcode];
	    p->first = p->prefix->first;
	    p->suffix = p->first;
	    outcode(p,fill);
	    if ((avail & codemask) == 0 && avail < 4096) {
		codesize++;
		codemask += avail;
	    }
	    oldcode = code;
	} else {
	    fatal("illegal code in raster data");
	}
}

/* Decode a raster image */

void readraster(UINT width,UINT height)
{
	UCHAR *fill = raster;
	UCHAR buf[255];
	int bits=0;
	UINT count;
	ULONG datum=0L;
	UCHAR *ch;
	int code;

	datasize = getc(infile);
	clear = 1 << datasize;
	eoi = clear+1;
	codesize = datasize + 1;
	codemask = (1 << codesize) - 1;
	for (code = 0; code < clear; code++) {
	    codetable[code].prefix = (codetype*)0;
	    codetable[code].first = code;
	    codetable[code].suffix = code;
	}
	for (count = getc(infile); count > 0; count = getc(infile)) {
	    fread(buf,1,count,infile);
	    for (ch=buf; count-- > 0; ch++) {
		datum += (long)*ch << bits;
		bits += 8;
		while (bits >= codesize) {
		    code = datum & codemask;
		    datum >>= codesize;
		    bits -= codesize;
		    if (code == eoi) goto exitloop;  /* This kludge put in
							because some GIF files
							aren't standard */
		    process(code,&fill);
		}
	    }
	}
exitloop:
	if (fill != raster + (long)width*height) fatal("raster has the wrong size");
}

/* Read a row out of the raster image and write it to the output file */

void rasterize(int row,int width)
{
	UCHAR *scanline;
	int i;

       scanline = raster + (long)row*width;
	for (i = 0; i < width; i++) {
	    if (i % 40 == 0) printf("\n");  /* break line every 80 chars */
	    fputs(colortable[*scanline++],stdout);
	}
	printf("\n");
}

/* write image trailer to standard output */

void writetrailer(void)
{
	printf("\n\ngrestore\n");
	if (!EPSF)
		printf("showpage\n");
}

/* Read image information (position, size, local color map, etc.) and convert
   to postscript. */

void readimage(void)
{
	UCHAR buf[9];
	UINT top,width,height;
	ULONG lwidth, lheight;
	bool local,interleaved;
	UCHAR localmap[256][3];
	int localbits;
	int *interleavetable;
	int row;
	int i;

	fread(buf,1,9,infile);
	top = buf[2] + (buf[3] << 8);
	lwidth = width = buf[4] + (buf[5] << 8);
	lheight = height = buf[6] + (buf[7] << 8);
	local = buf[8] & 0x80;
	interleaved = buf[8] & 0x40;
	if (local) {
	    localbits = (buf[8] & 0x7) + 1;
	    fread(localmap,3,1<<localbits,infile);
	    initcolors(colortable,localmap,1<<localbits);
	} else if (global) {
	    initcolors(colortable,globalmap,1<<globalbits);
	} else {
	    fatal("no colormap present for image");
	}
	writeheader(width,height);
	raster=(UCHAR *)malloc(lwidth*lheight);
	if (!raster) fatal("not enough memory for image");
	readraster(width,height);
	if (interleaved) {
	    interleavetable = (int*)malloc(lheight*sizeof(int));
	    if (!interleavetable) fatal("not enough memory for interleave table");
	    row = 0;
	    for (i = top; i < top+height; i += 8) interleavetable[i] = row++;
	    for (i = top+4; i < top+height; i += 8) interleavetable[i] = row++;
	    for (i = top+2; i < top+height; i += 4) interleavetable[i] = row++;
	    for (i = top+1; i < top+height; i += 2) interleavetable[i] = row++;
	    for (row = top; row < top+height; row++) rasterize(interleavetable[row],width);
	    free(interleavetable);
	} else {
	    for (row = top; row < top+height; row++) rasterize(row,width);
	}
	free(raster);
	writetrailer();
}

/* Read a GIF extension block (and do nothing with it). */

void readextension(void)
{
	int count;
	char buf[255];

	getc(infile);
	count=getc(infile);
	while (count)
	{
		fread(buf,1,count,infile);
		count=getc(infile);
	}
}

extern char    *optarg;
extern int      optind;

int main(int argc,char **argv)
{
 int quit = FALSE;
 int opt, posn = 0;
 char ch, gifname[40];
 char optarg[10];

 pagewidth = 8.5 * INCH;	    /* width, height of PostScript page	*/
 pageheight = 11.0 * INCH;
 imagewidth = 7.5 * INCH;    /* default max image height		*/
 imageheight = 9.0 * INCH;

 if(argc<2)
	usage();

 while((++posn < argc) && (argv[posn][0] == '-'))
 {
	opt = argv[posn][1];
	if(strlen(argv[posn]) > 2) strcpy(optarg, argv[posn]+2);


	switch (opt)
	{
		case 'c':
			if ((copies = atoi(optarg)) <= 0)
			fatal("#copies must be > 0");
			break;
		case 'e':
			EPSF = TRUE;
			break;
		case 'l':
			landscape = TRUE;
			break;
		case 's':
			screen = TRUE;
			break;
		case 'w':
			sscanf(optarg,"%lf",&imagewidth);
			imagewidth *= INCH;
			if (imagewidth <= 0.0)
				fatal("negative image width");
			break;
		case 'h':
			sscanf(optarg,"%lf",&imageheight);
			imageheight *= INCH;
			if (imageheight <= 0.0)
				fatal("negative image height");
			break;
		default:
			usage();
			break;
	}
 }
 strcpy(gifname, argv[posn]);
 infile = fopen(gifname,"rb");
 if (infile == NULL)
 {
	perror("giftops");
	exit(-1);
 }

 checksignature();
 readscreen();
 do
 {
	ch = getc(infile);
	switch (ch)
	{
		case '\0':  break;  /* this kludge for non-standard files */
		case ',':   readimage();
			    break;
		case ';':   quit = TRUE;
			    break;
		case '!':   readextension();
			    break;
		default:    fatal("illegal GIF block type");
			    break;
	 }
  } while (!quit);
  return 0;
}
