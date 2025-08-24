/*
    NDSForwarder for DSi
    Copyright (C) 2018-2020 JeffRuLz
    Copyright (C) 2022-present lifehackerhansol

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <alloca.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <nds.h>

#include "sav.h"
#include "main.h"
#include "message.h"
#include "maketmd.h"
#include "rom.h"
#include "storage.h"

#define wrapper_location 0x1DD4C
#define wrapper_location_fc 0x1962C
#define gamepath_location 0x1DD74
#define gamepath_location_fc 0x19655
#define gamepath_length 252
#define gamepath_length_fc 251

const char *_wrappers[][2] = {
  { "gb", "sd:/_nds/gameyob.nds" },
  { "gba", "sd:/_nds/gbarunner3.nds" },
  { "gbc", "sd:/_nds/gameyob.nds" },
};

static int _compareWrappers(const void *a, const void *b) {
  return strcmp(*(const char **)a, *(const char **)b);
}

const char *findWrapper(const char *fpath) {
  const char *fext = strrchr(fpath, '.');
  if (fext == NULL) {
    return NULL;
  }
  fext++;

  char *needle[2] = { strdup(fext), NULL };
  for (char *pNeedle = needle[0]; *pNeedle; pNeedle++) {
    *pNeedle = tolower(*pNeedle);
  }

  const char *wrapper = bsearch(&needle, _wrappers, sizeof(_wrappers) / sizeof(*_wrappers), sizeof(*_wrappers), _compareWrappers);
  if (wrapper != NULL) wrapper = ((const char **)wrapper)[1];
  free(needle[0]);

  return wrapper;
}

static bool _titleIsUsed(tDSiHeader* h)
{
	if (!h) return false;

	char path[64];
	sprintf(path, "/title/%08x/%08x/", (unsigned int)h->tid_high, (unsigned int)h->tid_low);

	return dirExists(path);
}

// randomize TID
static bool _patchGameCode(tDSiHeader* h)
{
	if (!h) return false;

	iprintf("Fixing Game Code...");
	swiWaitForVBlank();

	//set as standard app
	h->tid_high = 0x00030004;
		
	do {
		do {
			//generate a random game code
			for (int i = 0; i < 4; i++)
				h->ndshdr.gameCode[i] = 'A' + (rand() % 26);
		}
		while (h->ndshdr.gameCode[0] == 'A'); //first letter shouldn't be A

		//correct title id
		h->tid_low = ( (h->ndshdr.gameCode[0] << 24) | (h->ndshdr.gameCode[1] << 16) | (h->ndshdr.gameCode[2] << 8) | h->ndshdr.gameCode[3] );
	}
	while (_titleIsUsed(h));

	iprintf("\x1B[42m");	//green
	iprintf("Done\n");
	iprintf("\x1B[47m");	//white
	return true;
}

static bool _iqueHack(tDSiHeader* h)
{
	if (!h) return false;

	if (h->ndshdr.reserved1[8] == 0x80)
	{
		iprintf("iQue Hack...");	
		
		h->ndshdr.reserved1[8] = 0x00;

		iprintf("\x1B[42m");	//green
		iprintf("Done\n");
		iprintf("\x1B[47m");	//white
		return true;
	}

	return false;
}

static bool _checkSdSpace(unsigned long long size)
{
	iprintf("Enough room on SD card?...");
	swiWaitForVBlank();

	if (getSDCardFree() < size)
	{
		iprintf("\x1B[31m");	//red
		iprintf("No\n");
		iprintf("\x1B[47m");	//white
		return false;
	}

	iprintf("\x1B[42m");	//green
	iprintf("Yes\n");
	iprintf("\x1B[47m");	//white
	return true;
}

static bool _openMenuSlot()
{
	iprintf("Open DSi menu slot?...");
	swiWaitForVBlank();

	if (getMenuSlotsFree() <= 0)
	{
		iprintf("\x1B[31m");	//red
		iprintf("No\n");
		iprintf("\x1B[47m");	//white
		return choicePrint("Try installing anyway?");
	}

	iprintf("\x1B[42m");	//green
	iprintf("Yes\n");
	iprintf("\x1B[47m");	//white
	return true;
}

bool installError(char* error)
{
	iprintf("\x1B[31m");	//red
	iprintf("Error: ");
	iprintf("\x1B[33m");	//yellow
	iprintf("%s", error);
	iprintf("\x1B[47m");	//white
	
	messagePrint("\x1B[31m\nInstallation failed.\n\x1B[47m");
	return false;
}

static bool _generateFcForwarder(char* fpath, char* templatePath)
{
#if 0
  const char *fext = strrchr(fpath, '.');
  if (fext == NULL) {
    fext = "nds";
  } else {
    fext++;
  }
  char *needle = strdup(fext);
  char *pNeedle = needle;
  for (; *pNeedle; pNeedle++)
    *pNeedle = tolower(*pNeedle);

  const char *wrapper = bsearch(&fext, _wrappers, sizeof(_wrappers) / sizeof(*_wrappers), sizeof(*_wrappers), _compareWrappers);
  if (wrapper != NULL) wrapper = ((const char **)wrapper)[1];
  free(needle);
  pNeedle = needle = NULL;

	// extract template
	mkdir("/_nds", 0777);
	remove(templatePath);
	copyFile("nitro:/flashcard.nds", templatePath);
	iprintf("Template copied to SD.\n");

	tDSiHeader* templateheader = getRomHeader(templatePath);
	if(templateheader == NULL) return installError("Failed to read template header.\n");
	if (wrapper == NULL) {
    tNDSHeader *targetheader = getRomHeaderNDS(fpath);
    if(targetheader == NULL) return installError("Failed to read target header.\n");

    // header operations
    if(swiCRC16(0xFFFF, targetheader, 0x15E) != targetheader->headerCRC16) {
      free(targetheader);
      free(templateheader);
      return installError("Header CRC check failed. This ROM may be corrupt.\n");
    }

    free(targetheader);
  }

	// banner operations
	sNDSBannerExt* targetbanner = NULL;
	if (wrapper != NULL) {
    targetbanner = getRomBanner(fpath);
    if(targetbanner == NULL) return installError("Failed to read target banner.\n");
  } else {
    char bpath[strlen(fpath) + 5];
    strncpy(bpath, fpath, (strlen(fpath) - strlen(fext)) + 1);
    strcat(bpath, "bin");
    FILE *bf = fopen(bpath, "r");
    if (bf == NULL) return installError("Failed to open standalone banner (.bin) file for reading.\n");
    targetbanner = calloc(1, 0x840);
    if (fread(targetbanner, 0x840, 1, bf) != 0x840) {
      fclose(bf);
      free(targetbanner);
      return installError("Failed to read standalone banner (.bin) file.\n");
    }
    fclose(bf);
  }

  targetbanner->version = NDS_BANNER_VER_ORIGINAL;
  targetbanner->crc[1] = 0;
  targetbanner->crc[2] = 0;
  targetbanner->crc[3] = 0;

	// actually writing stuff now
	// write header
	FILE* template = fopen("fat:/_nds/template.nds", "rb+");
	fseek(template, 0, SEEK_SET);
	fwrite(templateheader, 1, sizeof(tDSiHeader), template);
	fflush(template);

	// write banner (excluding DSi part)
	fseek(template, templateheader->ndshdr.bannerOffset, SEEK_SET);
	fwrite(targetbanner, 0x840, 1, template);
	fflush(template);
	free(targetbanner);
	free(templateheader);
	
	// write game path
	fseek(template, gamepath_location_fc, SEEK_SET);
	fwrite(fpath, sizeof(char), gamepath_length_fc, template);
	fflush(template);

  // write wrapper path if needed
  if (wrapper != NULL) {
    fseek(template, wrapper_location_fc, SEEK_SET);
    fwrite(wrapper, sizeof(char), strlen(wrapper) + 1, template);
    fflush(template);
    iprintf("wrapper patched to %s\n", wrapper);
  }

	// complete
	fclose(template);
	iprintf("Forwarder created.\n\n");
	return true;
#else
  return installError("Currently unsupported.\n");
#endif
}

static bool _generateForwarder(char* fpath, char* templatePath)
{
  const char *wrapper = NULL;

  const char *ofext = strrchr(fpath, '.');
  if (ofext != NULL) {
    ofext++;
  }

  {
    const char *fext = ofext;
    if (fext == NULL) {
      fext = "nds";
    }

    char *needle[2] = { strdup(fext), NULL };
    for (char *pNeedle = needle[0]; *pNeedle; pNeedle++) {
      *pNeedle = tolower(*pNeedle);
    }

    iprintf("Template <- [%s] \"%s\".\n", needle[0], fpath);
    wrapper = bsearch(&needle, _wrappers, sizeof(_wrappers) / sizeof(*_wrappers), sizeof(*_wrappers), _compareWrappers);
    if (wrapper != NULL) wrapper = ((const char **)wrapper)[1];
    free(needle[0]);
  }

  if (ofext == NULL) {
    ofext = fpath + strlen(fpath);
  }

	// extract template
	mkdir("/_nds", 0777);
	remove(templatePath);
	copyFile("nitro:/sdcard.nds", templatePath);

	if (wrapper) iprintf("Using wrapper \"%s\".\n", wrapper);

	// DSiWare check
	if (wrapper == NULL) {
    tDSiHeader* targetDSiWareCheck = getRomHeader(fpath);
    if (targetDSiWareCheck == NULL) return installError("Failed to read template header.\n");
    if ((targetDSiWareCheck->tid_high & 0xFF) > 0)
    {
      bool choice = choicePrint("This is a DSiWare title!\nYou can install directly using\nTMFH instead, for full \ncompatibility.\nInstall anyway?");
      if(!choice) {
        free(targetDSiWareCheck);
        return installError("User cancelled install.\n");
      }
    }
    free(targetDSiWareCheck);
  }

	tDSiHeader* templateheader = getRomHeader(templatePath);
	if(templateheader == NULL) return installError("Failed to read template header.\n");
	if (wrapper == NULL) {
    tNDSHeader *targetheader = getRomHeaderNDS(fpath);
    if(targetheader == NULL) return installError("Failed to read target header.\n");

    // header operations
    if(swiCRC16(0xFFFF, targetheader, 0x15E) != targetheader->headerCRC16) {
      free(targetheader);
      free(templateheader);
      return installError("Header CRC check failed. This ROM may be corrupt.\n");
    }

    memcpy(templateheader->ndshdr.gameTitle, targetheader->gameTitle, 12);
    memcpy(templateheader->ndshdr.gameCode, targetheader->gameCode, 4);
    templateheader->tid_low = __builtin_bswap32((*(u32*)targetheader->gameCode));
    templateheader->ndshdr.headerCRC16 = swiCRC16(0xFFFF, &templateheader->ndshdr, 0x15E);
    free(targetheader);
  }

	// banner operations
	sNDSBannerExt* targetbanner = NULL;
  do {
    char bpath[strlen(fpath) + 5];
    memset(bpath, 0, sizeof(bpath));
    strcpy(bpath, fpath);
    strcat(bpath, ".bin");
    FILE *bf = fopen(bpath, "r");
    if (bf == NULL) {
      if (wrapper != NULL) {
        return installError("Failed to open standalone banner (.bin) file for reading.\n");
      } else {
        break;
      }
    }
    targetbanner = calloc(1, sizeof(sNDSBannerExt));
    if (!targetbanner) {
      fclose(bf);
      return installError("Out of memory.\n");
    }
    unsigned total = 0, read = 0;
    while ((read = fread(targetbanner, 1, sizeof(sNDSBannerExt), bf)) >= 0) {
      total += read;
      if (feof(bf)) {
        break;
      }
    }
    if (total < 0x840) {
      fclose(bf);
      free(targetbanner);
      return installError("Failed to read standalone banner (.bin) file.\n");
    }
    fclose(bf);
  } while (0) ;

  if (targetbanner == NULL) {
    targetbanner = getRomBanner(fpath);
    if(targetbanner == NULL) return installError("Failed to read target banner.\n");
  }

	// Only check up to ZH_KO. DSi is checked separately, and can be fixed by nulling the DSi data, but the rest needs to be intact.
	bool crccheck = true;
	switch(targetbanner->version) {
		case NDS_BANNER_VER_ZH_KO:
			if(swiCRC16(0xFFFF, &targetbanner->icon, 0xA20) != targetbanner->crc[2]) crccheck = false;
			break;
		case NDS_BANNER_VER_ZH:
			if(swiCRC16(0xFFFF, &targetbanner->icon, 0x920) != targetbanner->crc[1]) crccheck = false;
			break;
		case NDS_BANNER_VER_ORIGINAL:
			if(swiCRC16(0xFFFF, &targetbanner->icon, 0x820) != targetbanner->crc[0]) crccheck = false;
			break;
	}
	if (!crccheck) {
		free(targetbanner);
		free(templateheader);
		return installError("Icon/Title CRC check failed. This ROM may be corrupt.\n");
	}

	switch(targetbanner->version) {
		case NDS_BANNER_VER_ORIGINAL:
			memcpy(targetbanner->titles[6], targetbanner->titles[1], 0x100);
		case NDS_BANNER_VER_ZH:
			memcpy(targetbanner->titles[7], targetbanner->titles[1], 0x100);
		default:
			u16 crcDSi = swiCRC16(0xFFFF, &targetbanner->dsi_icon, 0x1180);
			if(targetbanner->version != NDS_BANNER_VER_DSi || crcDSi != targetbanner->crc[3]) {
				memset(targetbanner->reserved2, 0xFF, sizeof(targetbanner->reserved2));
				memset(targetbanner->dsi_icon, 0xFF, sizeof(targetbanner->dsi_icon));
				memset(targetbanner->dsi_palette, 0xFF, sizeof(targetbanner->dsi_palette));
				memset(targetbanner->dsi_seq, 0xFF, sizeof(targetbanner->dsi_seq));
				memset(targetbanner->reserved3, 0xFF, sizeof(targetbanner->reserved3));
				targetbanner->crc[3] = 0x0000;
				targetbanner->version = NDS_BANNER_VER_ZH_KO;
			} else targetbanner->crc[3] = crcDSi;
			targetbanner->crc[0] = swiCRC16(0xFFFF, &targetbanner->icon, 0x820);
			targetbanner->crc[1] = swiCRC16(0xFFFF, &targetbanner->icon, 0x920);
			targetbanner->crc[2] = swiCRC16(0xFFFF, &targetbanner->icon, 0xA20);
			break;
	}


	// actually writing stuff now
	// write header
	FILE* template = fopen("sd:/_nds/template.dsi", "rb+");
	fseek(template, 0, SEEK_SET);
	fwrite(templateheader, 1, sizeof(tDSiHeader), template);
	fflush(template);

	// write banner
	fseek(template, templateheader->ndshdr.bannerOffset, SEEK_SET);
	fwrite(targetbanner, sizeof(sNDSBannerExt), 1, template);
	fflush(template);
	free(targetbanner);
	free(templateheader);
	
	// write game path
	fseek(template, gamepath_location, SEEK_SET);
	fwrite(fpath, sizeof(char), gamepath_length, template);
	fflush(template);

  // write wrapper path if needed
  if (wrapper != NULL) {
    fseek(template, wrapper_location, SEEK_SET);
    fwrite(wrapper, sizeof(char), strlen(wrapper) + 1, template);
    fflush(template);
    iprintf("wrapper patched to %s\n", wrapper);
  }

	// complete
	fclose(template);
	iprintf("Forwarder created.\n\n");
	return true;
}

bool installFc(char* fpath)
{
	char fname[strlen(fpath)];
	{
		const int strLen = strlen(fpath);
		int fpathpos = strLen;
		while (fpath[fpathpos] != '/') {
			fpathpos--;
		}
		fpathpos++;
		for (int i = 0; i < strLen; i++) {
			fname[i] = fpath[fpathpos];
			if (fpath[fpathpos] == 0) break;
			fpathpos++;
		}
	}

	char* templatePath = "fat:/_nds/template.nds";

	if (!_generateFcForwarder(fpath, templatePath)) {
		return false;
	}

	tDSiHeader* h = getRomHeader(templatePath);	
	if (!h)
	{
		return installError("Could not open file.\n");
	}
	else
	{
		//get install size
		iprintf("Install Size: ");
		swiWaitForVBlank();
		
		unsigned long long fileSize = getRomSize(templatePath);

		printBytes(fileSize);
		iprintf("\n");

		if (!_checkSdSpace(fileSize)) return installError("Not enough space on SD.\n");

		//system title patch

		//create forwarder directory /forwarders
		mkdir("/forwarders", 0777);

		char newPath[12+strlen(fname)+1];
		sprintf(newPath, "/forwarders/%s", fname);	

		//check if forwarder exists
		if (access(newPath, F_OK) == 0)
		{
			char msg[40];
			sprintf(msg, "Forwarder already exists.\nReplace it?");

			if (choicePrint(msg) == NO) return installError("User cancelled install.\n");

			else
			{
				iprintf("\nDeleting:\n");
				deleteDir(newPath);
				iprintf("\n");
			}
		}

		{
			//create forwarder
			{
				iprintf("Creating forwarder...");
				swiWaitForVBlank();

				//copy nds file to forwarder folder
				{
					int result = copyFile(templatePath, newPath);

					if (result != 0)
					{
						char err[256];
						sprintf(err, "%s\n%s\n", newPath, strerror(errno));
						return installError(err);
					}

					iprintf("\x1B[42m");	//green
					iprintf("Done\n");
					iprintf("\x1B[47m");	//white
				}
			}
		}

		//end
		iprintf("\x1B[42m");	//green
		iprintf("\nInstallation complete.\n");
		iprintf("\x1B[47m");	//white
		iprintf("Back - [B]\n");
		keyWait(KEY_A | KEY_B);
	}
	free(h);
	// remove(templatePath);
	return true;
}

bool install(char* fpath, bool randomize)
{
	char* templatePath = "sd:/_nds/template.dsi";

	//confirmation message
	{
		char str[] = "Are you sure you want to install\n";
		char* msg = (char*)malloc(strlen(str) + strlen(fpath) + 8);
		sprintf(msg, "%s%s\n", str, fpath);
		
		bool choice = choiceBox(msg);
		free(msg);
		
		if (choice == NO)
			return false;
	}

	//start installation
	clearScreen(&bottomScreen);
	iprintf("Installing %s\n\n", fpath); swiWaitForVBlank();

	if (!isDSiMode()) {
		return installFc(fpath);
	}

	if (!_generateForwarder(fpath, templatePath)) {
		return false;
	}

	tDSiHeader* h = getRomHeader(templatePath);	
	if (!h)
	{
		return installError("Could not open file.\n");
	}
	else
	{
		bool fixHeader = false;

		if (randomize || (strcmp(h->ndshdr.gameCode, "####") == 0 && h->tid_low == 0x23232323) || (!*h->ndshdr.gameCode && h->tid_low == 0)) {
			if (_patchGameCode(h)) fixHeader = true;
			else return installError("Failed to randomize TID.\n");
		}

		//title id must be one of these
		if (!(h->tid_high == 0x00030004 ||
			  h->tid_high == 0x00030005 ||
			  h->tid_high == 0x00030015 ||
			  h->tid_high == 0x00030017))
			return installError("This is not a DSi ROM.\n");

		//get install size
		iprintf("Install Size: ");
		swiWaitForVBlank();
		
		unsigned long long fileSize = getRomSize(templatePath);

		printBytes(fileSize);
		iprintf("\n");

		if (!_checkSdSpace(fileSize)) return installError("Not enough space on SD.\n");

		//system title patch

		if (_iqueHack(h))
			fixHeader = true;

		//create title directory /title/XXXXXXXX/XXXXXXXX
		char dirPath[32];
		mkdir("/title", 0777);

		sprintf(dirPath, "/title/%08x", (unsigned int)h->tid_high);
		mkdir(dirPath, 0777);

		sprintf(dirPath, "/title/%08x/%08x", (unsigned int)h->tid_high, (unsigned int)h->tid_low);	

		//check if title is free
		if (_titleIsUsed(h))
		{
			char msg[64];
			sprintf(msg, "Title %08x is already used.\nInstall anyway?", (unsigned int)h->tid_low);

			if (choicePrint(msg) == NO) return installError("User cancelled install.\n");

			else
			{
				iprintf("\nDeleting:\n");
				deleteDir(dirPath);
				iprintf("\n");
			}
		}

		if (!_openMenuSlot())
			return installError("Not enough icon slots available.\n");

		mkdir(dirPath, 0777);

		//content folder /title/XXXXXXXX/XXXXXXXXX/content
		{
			char contentPath[64];
			sprintf(contentPath, "%s/content", dirPath);

			mkdir(contentPath, 0777);

			//create 00000000.app
			{
				iprintf("Creating 00000000.app...");
				swiWaitForVBlank();

				char appPath[80];
				sprintf(appPath, "%s/00000000.app", contentPath);

				//copy nds file to app
				{
					int result = copyFile(templatePath, appPath);

					if (result != 0)
					{
						char err[128];
						sprintf(err, "%s\n%s\n", appPath, strerror(errno));
						return installError(err);
					}

					iprintf("\x1B[42m");	//green
					iprintf("Done\n");
					iprintf("\x1B[47m");	//white
				}

				//pad out banner if it is the last part of the file
				{
					if (h->ndshdr.bannerOffset == fileSize - 0x1C00)
					{
						iprintf("Padding banner...");
						swiWaitForVBlank();

						if (padFile(appPath, 0x7C0) == false)
						{
							iprintf("\x1B[31m");	//red
							iprintf("Failed\n");
							iprintf("\x1B[47m");	//white
						}
						else
						{
							iprintf("\x1B[42m");	//green
							iprintf("Done\n");
							iprintf("\x1B[47m");	//white
						}
					}
				}

				//update header
				{
					if (fixHeader)
					{
						iprintf("Fixing header...");
						swiWaitForVBlank();

						//fix header checksum
						h->ndshdr.headerCRC16 = swiCRC16(0xFFFF, h, 0x15E);

						//fix RSA signature
						u8 buffer[20];
						swiSHA1Calc(&buffer, h, 0xE00);
						memcpy(&(h->rsa_signature[0x6C]), buffer, 20);

						FILE* f = fopen(appPath, "r+");

						if (!f)
						{
							iprintf("\x1B[31m");	//red
							iprintf("Failed\n");
							iprintf("\x1B[47m");	//white
						}
						else
						{
							fseek(f, 0, SEEK_SET);
							fwrite(h, sizeof(tDSiHeader), 1, f);

							iprintf("\x1B[42m");	//green
							iprintf("Done\n");
							iprintf("\x1B[47m");	//white
						}

						fclose(f);
					}
				}

				//make TMD
				{
					char tmdPath[80];
					sprintf(tmdPath, "%s/title.tmd", contentPath);

					if (maketmd(appPath, tmdPath) != 0)				
						return installError("Failed to generate TMD.\n");
				}
			}
		}

		//end
		iprintf("\x1B[42m");	//green
		iprintf("\nInstallation complete.\n");
		iprintf("\x1B[47m");	//white
		iprintf("Back - [B]\n");
		keyWait(KEY_A | KEY_B);
	}
	free(h);
	// remove(templatePath);
	return true;
}
