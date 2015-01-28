/*
 * Initially developed at Ghent University as part of a masters thesis promoted
 * by prof. dr. ir. Bjorn De Sutter of the Computer Systems Lab in cooperation with ir. Daan Raman from NVISO.
 * Author: Christophe Beauval
 * Version: 20140801
 * Description: Unpacks/repacks/packs the Android imgdata.img and converts to/from PNG
 * Instructions: Two options to compile: dynamic: gcc -o iunp imgdata_tool.c -lpng
 *                                       static: gcc -o iunp imgdata_tool.c -lpng -lz -static
 * Usage: $0 -l <imgdata.img> : list info and contents
 *           -x <imgdata.img> : extract contents in working dir
 *           -u <imgdata.img> <file1:X[:Y[:W[:H]]]> [...] : update "file1" in <imgdata.img> with given coordinates and size, use - to keep existing value
 *           -r <imgdata.img> <file1.png>[:X[:Y]] [...] : replace "file1" in <imgdata.img> with given file and optionally new coordinates
 *           -c <imgdata.img> <file1.png:X:Y> [...] : creates a new imgdata.img (overwriting any existing!) with contents rest of arguments
 *           X, Y, W, H are 32bit positive integers and can be given as 0x<HEX> and 0<OCT> as well
 *           "file1" name should not be longer than IMGDATA_FILE_NAME_SIZE chars, excluding extension, and be in current dir
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>

#include <png.h>

#define IMGDATA_MAGIC "IMGDATA!"
#define IMGDATA_MAGIC_SIZE 8 /* No room for terminating \0 */
#define IMGDATA_VERSION 1 /* value of unknown = version? */
#define IMGDATA_FILE_BLOCK_SIZE 512 /* content size has to be multiple of this, padded with zeros, in bytes */
#define IMGDATA_FILE_NAME_SIZE 16 /* max length of a filename (assuming not including terminating \0) */
#define IMGDATA_FILE_OFFSET_START 1024 /* start of the first imgdata file in bytes */

/* modes this program runs */
#define RUN_NONE 0
#define RUN_LIST 1
#define RUN_EXTRACT 2
#define RUN_UPDATE 3
#define RUN_REPLACE 4
#define RUN_CREATE 5

/* marks for changing metadata */
#define MARK_X 1
#define MARK_Y 2
#define MARK_W 4
#define MARK_H 8
#define MARK_S 16

/* imgdata.img header */
typedef struct {
	char magic[IMGDATA_MAGIC_SIZE];
	unsigned int unknown;
	unsigned int num_files;
	unsigned int padding_a;
	unsigned int padding_b;
} imgdatahdr;

/* part of the header, list of metadata of contents */
typedef struct {
	char name[IMGDATA_FILE_NAME_SIZE];
	unsigned int imgwidth; /* max 1080 for LG Nexus 5 */
	unsigned int imgheight; /* max 1920 for LG Nexus 5 */
	unsigned int scrxpos; /* pos on screen, 0 is leftmost */
	unsigned int scrypos; /* pos on screen, 0 is topmost */
	unsigned int offset; /* multiple of IMGDATA_FILE_BLOCK_SIZE */
	unsigned int size;
} imgdata_file;

/* basic unit of content */
typedef struct {
        unsigned char count;
        unsigned char red;
        unsigned char green;
        unsigned char blue;
} pixelrun;

/* representation of an encoded image */
typedef struct {
	char name[IMGDATA_FILE_NAME_SIZE + 1]; /* +1 for \0 */
	unsigned int size;
	pixelrun *content;
} imgdata_content;

/* internal representation of cmd args */
typedef struct {
	char name[IMGDATA_FILE_NAME_SIZE + 5]; /* 4 for ".png" and 1 for \0 */
	unsigned int x; /* pos on screen, 0 is leftmost */
	unsigned int y; /* pos on screen, 0 is topmost */
	unsigned int w; /* max 1080 for LG Nexus 5 */
	unsigned int h; /* max 1920 for LG Nexus 5 */
	unsigned int size; /* size */
	unsigned int bsize; /* size of content which is >= size as it's divisible by IMGDATA_FILE_BLOCK_SIZE */
	unsigned char mark;
	pixelrun *content;
} arg;

/*
 * Converts content to PNG
 */
int convert_to_png(pixelrun *buf, imgdata_file imgfile, FILE *out) {
	/* PNG structs */
	png_structp png_ptr;
	png_infop info_ptr;
	png_byte row[imgfile.imgwidth * 3]; /* 3 bytes per pixel */
	int i, j = 0, k;

	/* PNG inits */
	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr) {
		return EXIT_FAILURE;
	}

	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		png_destroy_write_struct(&png_ptr, NULL);
		return EXIT_FAILURE;
	}

	png_init_io(png_ptr, out);
	png_set_filter(png_ptr, 0, PNG_FILTER_VALUE_NONE);
	/* Fill IHDR */
	png_set_IHDR(png_ptr, info_ptr,
		imgfile.imgwidth,
		imgfile.imgheight,
		8,
		PNG_COLOR_TYPE_RGB,
		PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_DEFAULT,
		PNG_FILTER_TYPE_DEFAULT);

	png_write_info(png_ptr, info_ptr);

	/* Start making rows, go over the (imgfile.size / 4) pixelruns */
	for (i = 0; i < (imgfile.size >> 2); ++i) {
		for (k = 0; k < buf[i].count; ++k) {
			row[j] = buf[i].red;
			row[j+1] = buf[i].green;
			row[j+2] = buf[i].blue;
			if (j + 3 == imgfile.imgwidth * 3) {
				png_write_row(png_ptr, row);
				j = 0;
			} else {
				j += 3;
			}
		}
	}

	png_write_end(png_ptr, NULL);

	/* cleanup */
	png_destroy_write_struct(&png_ptr, &info_ptr);

	return EXIT_SUCCESS;
}

void print_usage(char *errmsg) {
	if (errmsg != NULL) {
		printf("Error: %s\n", errmsg);
	}
	printf("Usage: -l <imgdata.img> : list info and contents\n");
	printf("       -x <imgdata.img> : extract contents in working dir\n");
	printf("       -u <imgdata.img> <file1:X[:Y[:W[:H]]]> [...] : update \"file1\" in <imgdata.img> with given coordinates and size, use - to keep existing value\n");
	printf("       -r <imgdata.img> <file1.png>[:X[:Y]] [...] : replace \"file1\" in <imgdata.img> with given file and optionally new coordinates\n");
	printf("       -c <imgdata.img> <file1.png:X:Y> [...] : creates a new <imgdata.img> (overwriting any existing!) with contents rest of arguments\n");
	printf("       X, Y, W, H are 32bit positive integers and can be given as 0x<HEX> and 0<OCT> as well\n");
	printf("       \"file1\" name should not be longer than %d chars, excluding extension, and be in current dir\n", IMGDATA_FILE_NAME_SIZE);
}

/*
 * Reads the complete header of an imgdata.img
 * Returns EXIT_FAILURE if not a valid file or other problems
 */
int read_file_header(FILE *img, imgdatahdr *bimg, imgdata_file **imgs) {
	int read = 0;
	/* Read header without imgdata_file struct */
	read = fread(bimg, sizeof(imgdatahdr), 1, img);
	if (read <= 0) {
		return EXIT_FAILURE;
	}
	/* validate magic */
	if (strncmp(bimg->magic, IMGDATA_MAGIC, IMGDATA_MAGIC_SIZE)) {
		return EXIT_FAILURE;
	}

	/* read img_info headers */
	*imgs = malloc(bimg->num_files * sizeof(imgdata_file));
	if (*imgs == NULL) {
		return EXIT_FAILURE;
	}
	read = fread(*imgs, sizeof(imgdata_file), bimg->num_files, img);
	if (read <= 0) {
		free(imgs);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

/*
 * Creates a new header based on parsed arguments
 */
void create_file_header(imgdatahdr *bimg, imgdata_file **imgs, unsigned int count, arg ufile[]) {
	int i, check;
	char *dot;

	/* fill imgdatahdr */
	strncpy(bimg->magic, IMGDATA_MAGIC, IMGDATA_MAGIC_SIZE);
	bimg->unknown = IMGDATA_VERSION; /* wild guess */
	bimg->num_files = count;
	bimg->padding_a = 0;
	bimg->padding_b = 0;

	/* fill imgdata_file */
	*imgs = calloc(count, sizeof(imgdata_file)); /* everything zero */
	if (*imgs == NULL) {
		perror("Failed calloc of <*imgs> in create_file_header");
	}

	for (i = 0; i < count; ++i) {
		/* don't copy the file extension */
		dot = strrchr(ufile[i].name, '.');
		check = IMGDATA_FILE_NAME_SIZE;
		if (dot != NULL && dot - ufile[i].name < check) {
			check = dot - ufile[i].name;
		}
		/* have to use (*imgs)[i].X instead of imgs[i]->X to get correct address for i>0 */
		strncpy((*imgs)[i].name, ufile[i].name, check);
		(*imgs)[i].offset = IMGDATA_FILE_OFFSET_START;
	}
}

/*
 * Reads the encoded images of an imgdata.img
 * Returns EXIT_FAILURE if not a valid file or other problems
 */
int read_file_imgs(FILE *img, unsigned int count, imgdata_file *imgs, imgdata_content cont[]) {
	int i;

	for (i = 0; i < count; ++i) {
		strncpy(cont[i].name, imgs[i].name, IMGDATA_FILE_NAME_SIZE);
		cont[i].name[IMGDATA_FILE_NAME_SIZE] = '\0';

		/* get size in full block
		 * first -1 in case size == X*BLOCK_SIZE, after that +1 to get complete block */
		cont[i].size = (((imgs[i].size - 1) / IMGDATA_FILE_BLOCK_SIZE) + 1) * IMGDATA_FILE_BLOCK_SIZE;
		cont[i].content = malloc(cont[i].size);
		if (cont[i].content == NULL) {
			printf("Failed to allocate memory for %s: %s\n", cont[i].name, strerror(errno));
			return EXIT_FAILURE;
		}

		/* Read content */
		if (fseek(img, imgs[i].offset, SEEK_SET)) {
			return EXIT_FAILURE;
		}
		if (fread(cont[i].content, cont[i].size, 1, img) <= 0) {
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}

/*
 * Writes the complete header of an imgdata.img
 * Returns EXIT_FAILURE if not a valid file or other problems
 */
int write_file_header(FILE *img, imgdatahdr *bimg, imgdata_file **imgs) {
	int write = 0;

	/* back to start of file */
	rewind(img);

	/* write main header */
	write = fwrite(bimg, sizeof(imgdatahdr), 1, img);
	if (write <= 0) {
		return EXIT_FAILURE;
	}

	/* write img_info headers */
	write = fwrite(*imgs, sizeof(imgdata_file), bimg->num_files, img);
	if (write <= 0) {
		free(imgs);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

/*
 * Writes the given converted images in place of the existing
 * Returns EXIT_FAILURE if not a valid file or other problems
 */
int write_file_imgs(FILE *img, imgdata_content cont[], unsigned int icount, arg ufile[], unsigned int ucount) {
	int i, j, written;

	/* to start of file content */
	if (fseek(img, IMGDATA_FILE_OFFSET_START, SEEK_SET)) {
		return EXIT_FAILURE;
	}

	for (i = 0; i < icount; ++i) {
		written = 0;
		for (j = 0; j < ucount; ++j) {
			if (!strncmp(cont[i].name, ufile[j].name, strlen(cont[i].name))) {
				if (fwrite(ufile[j].content, ufile[j].bsize, 1, img) <= 0) {
					return EXIT_FAILURE;
				}
				written = 1;
			}
		}

		/* if not written, content is not replaced, so put back original */
		if (!written) {
			if (fwrite(cont[i].content, cont[i].size, 1, img) <= 0) {
				return EXIT_FAILURE;
			}
		}
	}

	/* truncate file to exact length */
	if (ftruncate(fileno(img), ftell(img))) {
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

/*
 * Writes the parsed files only, to be used when creating a new container
 */
int write_file_args(FILE *img, arg ufile[], unsigned int count) {
	int i;

	/* flush and truncate file to exact start of content */
	if (fflush(img)) {
		return EXIT_FAILURE;
	}
	if (ftruncate(fileno(img), IMGDATA_FILE_OFFSET_START)) {
		return EXIT_FAILURE;
	}

	/* to start of file content */
	if (fseek(img, IMGDATA_FILE_OFFSET_START, SEEK_SET)) {
		return EXIT_FAILURE;
	}

	/* write new imgs */
	for (i = 0; i < count; ++i) {
		if (fwrite(ufile[i].content, ufile[i].bsize, 1, img) <= 0) {
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}

/*
 * Prints the info from the imgdata.img header
 */
void list_header_info(imgdatahdr *bimg, imgdata_file *imgs) {
	int i;
	/* Show complete magic, need to copy so we can terminate */
	char magicstr[IMGDATA_MAGIC_SIZE + 1];
	strncpy(magicstr, bimg->magic, IMGDATA_MAGIC_SIZE);
	magicstr[IMGDATA_MAGIC_SIZE] = '\0';

	printf("magic: %s\n", magicstr);
	printf("unknown: %d\n", bimg->unknown);
	printf("num_files: %d\n", bimg->num_files);
	printf("padding_a: %d\n", bimg->padding_a);
	printf("padding_b: %d\n", bimg->padding_b);

	printf("                           \twidth\theight\tx-pos\ty-pos\toffset\tsize\n");
	for (i = 0; i < bimg->num_files; ++i) {
		printf("File %02d = %16s:\t%d\t%d\t%d\t%d\t%d\t%d\n", i, imgs[i].name, imgs[i].imgwidth, imgs[i].imgheight, imgs[i].scrxpos, imgs[i].scrypos, imgs[i].offset, imgs[i].size);
	}
}

/*
 * Extracts the images and converts them to PNG
 */
void extract_contents(FILE *img, int num_files, imgdata_file *imgs) {
	FILE *out;
	pixelrun *buf;
	int i;

	for (i = 0; i < num_files; ++i) {
		char outfile[strlen(imgs[i].name) + 5];
		sprintf(outfile, "%s.png", imgs[i].name);
		if (!(out = fopen(outfile, "w+"))) {
			perror("Error opening file");
			continue;
		}
		printf("%s\n", outfile);

		/* Read content to buffer */
		fseek(img, imgs[i].offset, SEEK_SET);
		buf = malloc(imgs[i].size);
		/* Read in completely, sizes are limited with upperbound
		   max partition size (3MiB on LG Nexus 5) minus header (1KiB)*/
		fread(buf, imgs[i].size, 1, img);

		/* Convert to PNG */
		if (convert_to_png(buf, imgs[i], out) == EXIT_FAILURE) {
			printf("Error converting %s to PNG.\n", imgs[i].name);
		}

		if (buf != NULL) free(buf);
		fclose(out);
	}
}

/*
 * Parses the given file(name)s for their coords (and size)
 * A MARK_* is put in the arg.mark for each changing value
 */
void parse_args(unsigned int count, char *args[], arg ifile[]) {
	int i, max;
	char *str, *dot;

	for (i = 0; i < count; ++i) {
		ifile[i].mark = 0;
		str = strtok(args[i], ":");
		dot = strrchr(args[i], '.');
		max = sizeof(ifile[i].name) - 1;
		if ((dot != NULL && dot - args[i] > IMGDATA_FILE_NAME_SIZE) ||
			(str != NULL && strlen(str) > max) ||
			(dot == NULL && str != NULL && strlen(str) > IMGDATA_FILE_NAME_SIZE)
		) {
			printf("Filename %s too long, skipping\n", str);
			continue;
		}
		strncpy(ifile[i].name, str, max);
		ifile[i].name[max] = '\0';

		/* X-pos */
		str = strtok(NULL, ":");
		if (str != NULL) {
			if (*str != '-') {
				ifile[i].x = (unsigned int) strtol(str, NULL, 0);
				ifile[i].mark += MARK_X;
			}
			/* Y-pos */
			str = strtok(NULL, ":");
			if (str != NULL) {
				if (*str != '-') {
					ifile[i].y = (unsigned int) strtol(str, NULL, 0);
					ifile[i].mark += MARK_Y;
				}
				/* width */
				str = strtok(NULL, ":");
				if (str != NULL) {
					if (*str != '-') {
						ifile[i].w = (unsigned int) strtol(str, NULL, 0);
						ifile[i].mark += MARK_W;
					}
					/* height */
					str = strtok(NULL, ":");
					if (str != NULL) {
						if (*str != '-') {
							ifile[i].h = (unsigned int) strtol(str, NULL, 0);
							ifile[i].mark += MARK_H;
						}
					}
				}
			}
		}
	}
}

/*
 * Updates the header with new coords and sizes
 */
void update_header(imgdata_file *imgs, unsigned int icount, arg ufile[], unsigned int ucount) {
	int i, j, check, offchange = 0;
	char *dot;
	char uname[IMGDATA_FILE_NAME_SIZE + 1];

	/* loop over packed imgs */
	for (j = 0; j < icount; ++j) {
		/* add or subtract possible offset change */
		imgs[j].offset += offchange;

		/* loop over parsed imgs */
		for (i = 0; i < ucount; ++i) {
			/* can't use strlen on imgs[j].name as it's uncertain it's nullterminated */
			dot = strrchr(ufile[i].name, '.');
			check = IMGDATA_FILE_NAME_SIZE;
			if (dot != NULL && dot - ufile[i].name < check) {
				check = dot - ufile[i].name;
			}
			strncpy(uname, ufile[i].name, check);
			uname[check] = '\0';
			if (!strncmp(uname, imgs[j].name, IMGDATA_FILE_NAME_SIZE)) {
				if (ufile[i].mark & MARK_X) {
					imgs[j].scrxpos = ufile[i].x;
				}
				if (ufile[i].mark & MARK_Y) {
					imgs[j].scrypos = ufile[i].y;
				}
				if (ufile[i].mark & MARK_W) {
					imgs[j].imgwidth = ufile[i].w;
				}
				if (ufile[i].mark & MARK_H) {
					imgs[j].imgheight = ufile[i].h;
				}
				if (ufile[i].mark & MARK_S) {
					/* get how many blocks were used and are used now */
					int iblks, ublks;
					/* iblks can be 0 when adding a new image */
					iblks = imgs[j].size == 0 ? 0 : (imgs[j].size / IMGDATA_FILE_BLOCK_SIZE) + 1;
					ublks = (ufile[i].size / IMGDATA_FILE_BLOCK_SIZE) + 1;
					/* if different amount of blocks are used, update offchange */
					if (ublks - iblks) {
						offchange += (IMGDATA_FILE_BLOCK_SIZE * (ublks - iblks));
					}
					imgs[j].size = ufile[i].size;
				}
			}
		}
	}
}

/*
 * Parses the given files and extracts the size and converts the image to the imgdata format
 */
void parse_png_files(unsigned int count, arg ufile[]) {
	int i, num = 8;
	png_byte header[num];
	FILE *fp;
	png_structp png_ptr;
	png_infop info_ptr;
	png_color_16 my_background = {0, 0, 0, 0, 0};
	png_color_16p image_background;
	png_uint_32 width= 0, height = 0;
	png_byte color_type = 0;
	png_byte bit_depth = 0;

	for (i = 0; i < count; ++i) {
		if (!(fp = fopen(ufile[i].name, "rb"))) {
			printf("Problem opening file %s, skipping: %s\n", ufile[i].name, strerror(errno));
			continue;
		}

		if (fread(header, 1, num, fp) <= 0) {
			printf("Problem reading file %s, skipping: %s\n", ufile[i].name, strerror(errno));
			fclose(fp);
			continue;
		}
		if (png_sig_cmp(header, 0, num)) {
			printf("Problem reading file %s, skipping: not a PNG file\n", ufile[i].name);
			fclose(fp);
			continue;
		}

		png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
		if (!png_ptr) {
			printf("Problem creating PNG structs for %s, skipping\n", ufile[i].name);
			fclose(fp);
			continue;
		}


		info_ptr = png_create_info_struct(png_ptr);
		if (!info_ptr) {
			printf("Problem creating PNG structs for %s, skipping\n", ufile[i].name);
			png_destroy_read_struct(&png_ptr, NULL, NULL);
			fclose(fp);
			continue;
		}

		png_init_io(png_ptr, fp);
		png_set_sig_bytes(png_ptr, num);

		png_read_info(png_ptr, info_ptr);

		/* Clear alpa channel by making it black */
		if (png_get_bKGD(png_ptr, info_ptr, &image_background)) {
			png_set_background(png_ptr, image_background, PNG_BACKGROUND_GAMMA_FILE, 1, 1.0);
		} else {
			png_set_background(png_ptr, &my_background, PNG_BACKGROUND_GAMMA_SCREEN, 0, 1.0);
		}

		/* get and set width and height */
		width = png_get_image_width(png_ptr, info_ptr);
		height = png_get_image_height(png_ptr, info_ptr);
		ufile[i].w = (unsigned int) width;
		ufile[i].mark += MARK_W;
		ufile[i].h = (unsigned int) height;
		ufile[i].mark += MARK_H;

		/* transform PNG to RGB with 8bit depth */
		color_type = png_get_color_type(png_ptr, info_ptr);
		bit_depth= png_get_bit_depth(png_ptr, info_ptr);
		if (color_type == PNG_COLOR_TYPE_PALETTE) {
			png_set_palette_to_rgb(png_ptr);
		}
		if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
			png_set_gray_to_rgb(png_ptr);
			if(bit_depth < 8) {
				png_set_expand_gray_1_2_4_to_8(png_ptr);
			}
		}
		if (bit_depth == 16) {
			png_set_strip_16(png_ptr);
		}

		png_read_update_info(png_ptr, info_ptr);

		/* get pixels and transform to imgdata format */
		if (height > 0 && width > 0) {
			png_bytep rows[height];
			int j, k, l = 0, blockcount = 1;
			unsigned int prsize = sizeof(pixelrun);
			/* should be width * 3 */
			int bwidth = png_get_rowbytes(png_ptr,info_ptr);

			for (j = 0; j < height; ++j) {
				rows[j] = malloc(bwidth);
			}
			png_read_image(png_ptr, rows);

			/* start with 1 block, realloc if more needed */
			ufile[i].content = malloc(IMGDATA_FILE_BLOCK_SIZE);

			/* init first pixelrun */
			if (height > 0 && bwidth > 2) {
				ufile[i].content[l].red = rows[0][0];
				ufile[i].content[l].green = rows[0][1];
				ufile[i].content[l].blue = rows[0][2];
				/* count 0 as it'll be increased in the actual filling */
				ufile[i].content[l].count = 0;
			}

			/* actual filling */
			for (j = 0; j < height; ++j) {
				for (k = 0; k < bwidth; k+=3) {
					if (ufile[i].content[l].count != 255 &&
						ufile[i].content[l].red == rows[j][k] &&
						ufile[i].content[l].green == rows[j][k+1] &&
						ufile[i].content[l].blue == rows[j][k+2]
					) {
						++(ufile[i].content[l].count);
					} else {
						++l;
						if (IMGDATA_FILE_BLOCK_SIZE * blockcount <= l * prsize) {
							++blockcount;
							ufile[i].content = realloc(ufile[i].content, blockcount * IMGDATA_FILE_BLOCK_SIZE);
						}
						ufile[i].content[l].red = rows[j][k];
						ufile[i].content[l].green = rows[j][k+1];
						ufile[i].content[l].blue = rows[j][k+2];
						ufile[i].content[l].count = 1;
					}
				}
			}
			++l;
			ufile[i].bsize = blockcount * IMGDATA_FILE_BLOCK_SIZE;
			ufile[i].size = l * prsize;
			ufile[i].mark += MARK_S;

			/* zero remainder of block for niceness */
			while (l * prsize < ufile[i].bsize) {
				ufile[i].content[l].red = 0;
				ufile[i].content[l].green = 0;
				ufile[i].content[l].blue = 0;
				ufile[i].content[l].count = 0;
				++l;
			}

			/* cleanup */
			for (j = 0; j < height; ++j) {
				free(rows[j]);
			}
		}
	}
}

/*
 * Free the allocated memory for the content
 */
void free_file_imgs(imgdata_content conts[], unsigned int count) {
	while (count) {
		if (conts[count - 1].content != NULL) {
			free(conts[count - 1].content);
		}
		--count;
	}
}

/*
 * Free the allocated memory for the given files
 */
void free_file_args(arg ufile[], unsigned int count) {
	while (count) {
		if (ufile[count - 1].content != NULL) {
			free(ufile[count - 1].content);
		}
		--count;
	}
}

int main(int argc, char **argv) {
	FILE *img;
	char fmode[4];
	imgdatahdr bimg;
	imgdata_file *imgs;
	unsigned char mode = RUN_NONE;
	unsigned int count = argc < 3 ? 0 : argc - 3;

	/* default mode is reading, set double \0 for "-u" needing mode rb+ */
	strncpy(fmode, "rb", 4);

	if (argc <= 2) {
		print_usage(NULL);
	} else if (argv[1][0] == '-' && argv[1][1] == 'l' && argv[1][2] == '\0') {
		if (argc == 3) {
			mode = RUN_LIST;
		} else {
			print_usage("give one argument denoting the imgdata.img");
		}
	} else if (argv[1][0] == '-' && argv[1][1] == 'x' && argv[1][2] == '\0') {
		if (argc == 3) {
			mode = RUN_EXTRACT;
		} else {
			print_usage("give one argument denoting the imgdata.img");
		}
	} else if (argv[1][0] == '-' && argv[1][1] == 'u' && argv[1][2] == '\0') {
		if (argc >= 4) {
			mode = RUN_UPDATE;
			fmode[0] = 'r';
			fmode[2] = '+';
		} else {
			print_usage("give one argument denoting the imgdata.img and one or more imagenames to update in it");
		}
	} else if (argv[1][0] == '-' && argv[1][1] == 'r' && argv[1][2] == '\0') {
		if (argc >= 4) {
			mode = RUN_REPLACE;
			fmode[0] = 'r';
			fmode[2] = '+';
		} else {
			print_usage("give one argument denoting the imgdata.img and one or more images to replace in it");
		}
	} else if (argv[1][0] == '-' && argv[1][1] == 'c' && argv[1][2] == '\0') {
		if (argc >= 4) {
			mode = RUN_CREATE;
			fmode[0] = 'w';
		} else {
			print_usage("give one argument denoting the imgdata.img and one or more images to add in it");
		}
	} else {
		print_usage("give one argument denoting the imgdata.img and one or more images to update in it");
	}

	if (mode == RUN_NONE) {
		return EXIT_FAILURE;
	}

	if (!(img = fopen(argv[2], fmode))) {
		perror("Error opening file");
		return EXIT_FAILURE;
	}

	switch (mode) {
		case RUN_LIST:
			if (read_file_header(img, &bimg, &imgs) == EXIT_FAILURE) {
				print_usage("not a valid imgdata.img");
			} else {
				list_header_info(&bimg, imgs);
			}
			break;
		case RUN_EXTRACT:
			if (read_file_header(img, &bimg, &imgs) == EXIT_FAILURE) {
				print_usage("not a valid imgdata.img");
			} else {
				extract_contents(img, bimg.num_files, imgs);
			}
			break;
		case RUN_UPDATE:
			if (read_file_header(img, &bimg, &imgs) == EXIT_FAILURE) {
				print_usage("not a valid imgdata.img");
			} else {
				arg ufile[count];
				parse_args(count, &argv[3], ufile);
				update_header(imgs, bimg.num_files, ufile, count);
				if (write_file_header(img, &bimg, &imgs) == EXIT_FAILURE) {
					printf("An error occured writing the updated header information\n");
				}
			}
			break;
		case RUN_REPLACE:
			if (read_file_header(img, &bimg, &imgs) == EXIT_FAILURE) {
				print_usage("not a valid imgdata.img");
			} else {
				arg ufile[count];
				imgdata_content conts[bimg.num_files];

				parse_args(count, &argv[3], ufile);
				parse_png_files(count, ufile);

				if (read_file_imgs(img, bimg.num_files, imgs, conts) == EXIT_FAILURE) {
					printf("An error occured getting the encoded content\n");
				} else {
					update_header(imgs, bimg.num_files, ufile, count);
					if (write_file_header(img, &bimg, &imgs) == EXIT_FAILURE) {
						printf("An error occured writing the updated header information\n");
					} else if (write_file_imgs(img, conts, bimg.num_files, ufile, count) == EXIT_FAILURE) {
						printf("An error occured writing the replaced image file\n");
					}
				}
				free_file_imgs(conts, bimg.num_files);
				free_file_args(ufile, count);
			}
			break;
		case RUN_CREATE:
			{
				arg ufile[count];
				parse_args(count, &argv[3], ufile);
				parse_png_files(count, ufile);

				create_file_header(&bimg, &imgs, count, ufile);

				update_header(imgs, count, ufile, count);
				if (write_file_header(img, &bimg, &imgs) == EXIT_FAILURE) {
					printf("An error occured writing the new header information\n");
				} else if (write_file_args(img, ufile, count) == EXIT_FAILURE) {
					printf("An error occured writing the new image file\n");
				}
				free_file_args(ufile, count);
			}
			break;
		default:
			print_usage("unknown mode to run in");
			return EXIT_FAILURE;
	}

	/* Cleanup */
	if (imgs != NULL) free(imgs);
	fclose(img);

	return EXIT_SUCCESS;
}
