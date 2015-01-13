/*
 * Initially developed at Ghent University as part of a masters thesis promoted
 * by prof. dr. ir. Bjorn De Sutter of the Computer Systems Lab in cooperation with ir. Daan Raman from NVISO.
 * Author: Christophe Beauval
 * Version: 20140302
 * Description: Unpacks the Android bootloader.img
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>

/* from AOSP device/lge/hammerhead/releasetools.py */
/* unsigned int are in big endian */

#define BOOTLDR_MAGIC "BOOTLDR!"
#define BOOTLDR_MAGIC_SIZE 8 /* No room for terminating \0 */

typedef struct {
	char magic[BOOTLDR_MAGIC_SIZE];
	unsigned int num_images;
	unsigned int start_offset;
	unsigned int bootldr_size;
} bootldrimgh;

typedef struct {
	char name[64];
	unsigned int size;
} img_info;

int main(int argc, char **argv) {
	FILE *img, *out;
	void *buf;
	char *outname;
	bootldrimgh bimg;
	img_info *imgs;
	unsigned int i = 0;

	if ((argc != 2 && argc != 3) || (argc == 3 && (argv[1][0] != '-' || argv[1][1] != 'v'))) {
		printf("Usage: %s [-v] <bootloader.img>\n", argv[0]);
		return EXIT_FAILURE;
	}

	if (argc == 2 && !(img = fopen(argv[1], "r"))) {
		perror("Error opening file");
		return EXIT_FAILURE;
	}
	if (argc == 3 && !(img = fopen(argv[2], "r"))) {
		perror("Error opening file");
		return EXIT_FAILURE;
	}

	/* Read header without img_info struct */
	fread(&bimg, sizeof(bootldrimgh), 1, img);
	/* for printing only */
	if (argc == 3) {
		bimg.magic[BOOTLDR_MAGIC_SIZE-1] = 0;
		printf("magic: %s\n", bimg.magic);
		printf("num_images: %d\n", bimg.num_images);
		printf("start_offset: %d\n", bimg.start_offset);
		printf("bootldr_size: %d\n", bimg.bootldr_size);
	}

	/* read img_info headers */
	imgs = malloc(bimg.num_images * sizeof(img_info));
	fread(imgs, sizeof(img_info), bimg.num_images, img);

	fseek(img, bimg.start_offset, SEEK_SET);

	for (i = 0; i < bimg.num_images; ++i) {
		/* Output to name.img, needing 5 more chars of mem */
		outname = malloc(strlen(imgs[i].name) + 5);
		sprintf(outname, "%s.img", imgs[i].name);
		if (!(out = fopen(outname, "w+"))) {
			perror("Error opening file");
			return EXIT_FAILURE;
		}
		if (argc == 3) {
			printf("Unpacking image %d = %s to %s (size: %d)\n", i + 1, imgs[i].name, outname, imgs[i].size);
		} else {
			printf("%s\n", imgs[i].name);
		}
		if (outname != NULL) free(outname);

		/* Read part of file to buffer */
		buf = malloc(imgs[i].size);
		fread(buf, imgs[i].size, 1, img);

		/* Write buffer to file */
		fwrite(buf, imgs[i].size, 1, out);

		fclose(out);
		if (buf != NULL) free(buf);
	}

	/* Cleaup */
	if (imgs != NULL) free(imgs);
	fclose(img);

	return EXIT_SUCCESS;
}
