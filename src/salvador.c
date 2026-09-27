/*
 * salvador.c - command line compression utility for the salvador library
 *
 * Copyright (C) 2021 Emmanuel Marty
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

/*
 * Uses the libdivsufsort library Copyright (c) 2003-2008 Yuta Mori
 *
 * Implements the ZX0 encoding designed by Einar Saukas. https://github.com/einar-saukas/ZX0
 * Also inspired by Charles Bloom's compression blog. http://cbloomrants.blogspot.com/
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <sys/timeb.h>
#else
#include <sys/time.h>
#endif
#include "libsalvador.h"
#include "ELF.h"
#include "zx0.h"

#define OPT_VERBOSE        1
#define OPT_STATS          2
#define OPT_BACKWARD       4
#define OPT_CLASSIC        8

#define TOOL_VERSION "1.4.2"
#define PS2_VERSION "1.1"

/*---------------------------------------------------------------------------*/

#ifdef _WIN32
LARGE_INTEGER hpc_frequency;
BOOL hpc_available = FALSE;
#endif

static void do_init_time() {
#ifdef _WIN32
   hpc_frequency.QuadPart = 0;
   hpc_available = QueryPerformanceFrequency(&hpc_frequency);
#endif
}

static long long do_get_time() {
   long long nTime;

#ifdef _WIN32
   if (hpc_available) {
      LARGE_INTEGER nCurTime;

      /* Use HPC hardware for best precision */
      QueryPerformanceCounter(&nCurTime);
      nTime = (long long)(nCurTime.QuadPart * 1000000LL / hpc_frequency.QuadPart);
   }
   else {
      struct _timeb tb;
      _ftime(&tb);

      nTime = ((long long)tb.time * 1000LL + (long long)tb.millitm) * 1000LL;
   }
#else
   struct timeval tm;
   gettimeofday(&tm, NULL);

   nTime = (long long)tm.tv_sec * 1000000LL + (long long)tm.tv_usec;
#endif
   return nTime;
}

/*---------------------------------------------------------------------------*/

static void compression_progress(long long nOriginalSize, long long nCompressedSize) {
   if (nOriginalSize >= 512 * 1024) {
      fprintf(stdout, "\r%lld => %lld (%g %%)     \b\b\b\b\b", nOriginalSize, nCompressedSize, (double)(nCompressedSize * 100.0 / nOriginalSize));
      fflush(stdout);
   }
}

static int do_compress(const char *pszInFilename, const char *pszOutFilename, const unsigned int nOptions, const unsigned int nMaxWindowSize) {
   long long nStartTime = 0LL, nEndTime = 0LL;
   size_t nOriginalSize = 0L, nOriginalSizeSaved = 0L, nCompressedSize = 0L, nMaxCompressedSize;
   int nFlags = (nOptions & OPT_CLASSIC) ? 0 : FLG_IS_INVERTED;
   salvador_stats stats;
   unsigned char *pDecompressedData;
   unsigned char *pParsedData;
   unsigned char *pCompressedData;

   if (nOptions & OPT_BACKWARD)
      nFlags |= FLG_IS_BACKWARD;

   if (nOptions & OPT_VERBOSE) {
      nStartTime = do_get_time();
   }

   /* Read the whole original file in memory */

   FILE *f_in = fopen(pszInFilename, "rb");
   if (!f_in) {
      fprintf(stderr, "error opening '%s' for reading\n", pszInFilename);
      return 100;
   }

   fseek(f_in, 0, SEEK_END);
   nOriginalSize = (size_t)ftell(f_in);
   fseek(f_in, 0, SEEK_SET);

   nOriginalSizeSaved = nOriginalSize;
   pDecompressedData = (unsigned char*)malloc(nOriginalSize);
   if (!pDecompressedData) {
      fclose(f_in);
      fprintf(stderr, "out of memory for reading '%s', %zu bytes needed\n", pszInFilename, nOriginalSize);
      return 100;
   }

   /* Read input file data */

   if (fread(pDecompressedData, 1, nOriginalSize, f_in) != nOriginalSize) {
      free(pDecompressedData);
      fclose(f_in);
      fprintf(stderr, "I/O error while reading '%s'\n", pszInFilename);
      return 100;
   }

   fclose(f_in);
   f_in = NULL;

   if(!Check_ELF(pDecompressedData))
   {
      free(pDecompressedData);
      printf("\nInput is not a valid PS2 ELF file\n");
      return 100;
   }
   pParsedData = Parse_ELF(pDecompressedData, (int) nOriginalSize);
   if(pParsedData == NULL)
   {
      free(pDecompressedData);
      printf("\nCorrupted ELF file\n");
      return 100;
   }
   free(pDecompressedData);
   pDecompressedData = pParsedData;
   nOriginalSize = Get_ELF_Total_Size();

   printf("\nStarting address: 0x%x\n", (unsigned int) Get_ELF_Base_Address());
   printf("Entry point: 0x%x\n", (unsigned int) Get_ELF_Entry_Point());

   /* Allocate max compressed size */

   nMaxCompressedSize = salvador_get_max_compressed_size(nOriginalSize);

   pCompressedData = (unsigned char*)malloc(nMaxCompressedSize);
   if (!pCompressedData) {
      free(pDecompressedData);
      fprintf(stderr, "\nout of memory for compressing '%s', %zu bytes needed\n", pszInFilename, nMaxCompressedSize);
      return 100;
   }

   memset(pCompressedData, 0, nMaxCompressedSize);

   nCompressedSize = salvador_compress(pDecompressedData, pCompressedData, nOriginalSize, nMaxCompressedSize, nFlags, nMaxWindowSize, 0, compression_progress, &stats);

   if (nOptions & OPT_VERBOSE) {
      nEndTime = do_get_time();
   }

   if (nCompressedSize == (size_t)-1) {
      free(pCompressedData);
      free(pDecompressedData);
      fprintf(stderr, "\ncompression error for '%s'\n", pszInFilename);
      return 100;
   }

   /* Write whole compressed file out */

   FILE *f_out = fopen(pszOutFilename, "wb");
   if (!f_out) {
      free(pCompressedData);
      free(pDecompressedData);
      fprintf(stderr, "\nerror opening '%s' for writing\n", pszOutFilename);
      return 100;
   }

   elf_header_t File_Header;
   elf_small_pheader_t Section_Header; 
   u32 Depacking_Source;
   u32 Depacking_Dest;
   u32 Depacked_Entry;

   File_Header.ident[0] = 0x7f;
   File_Header.ident[1] = 0x45;
   File_Header.ident[2] = 0x4c;
   File_Header.ident[3] = 0x46;
   File_Header.ident[4] = 0x01;
   File_Header.ident[5] = 0x01;
   File_Header.ident[6] = 0x01;
   File_Header.ident[7] = 0x00;
   File_Header.ident[8] = 0x48;
   File_Header.ident[9] = 0x49;
   File_Header.ident[10] = 0x54;
   File_Header.ident[11] = 0x43;
   File_Header.ident[12] = 0x48;
   File_Header.ident[13] = 0x00;
   File_Header.ident[14] = 0x00;
   File_Header.ident[15] = 0x00;
   File_Header.type = 2;
   File_Header.machine = 8;
   File_Header.version = 1;
   // Put it after the depacked data
   File_Header.entry = Get_ELF_Base_Address() + (u32) ((nOriginalSize >> 4) << 4);
   File_Header.phoff = 0x34;
   File_Header.shoff = 0;
   File_Header.flags = 0;
   File_Header.ehsize = 52;
   File_Header.phentsize = 32;
   File_Header.phnum = 1;
   File_Header.shentsize = 0;
   File_Header.shnum = 0;
   File_Header.shstrndx = 0;

   Section_Header.type = PT_LOAD;
   Section_Header.offset = 0x50;
   Section_Header.vaddr = File_Header.entry;
   Section_Header.paddr = File_Header.entry;
   Section_Header.filesz = (u32) nCompressedSize + size_zx0_bin;
   Section_Header.memsz = Section_Header.filesz;
   Section_Header.flags = PF_X | PF_W | PF_R;

   Depacking_Source = size_zx0_bin + File_Header.entry;
   Depacking_Dest = Get_ELF_Base_Address();
   // Construct a jump opcode
   Depacked_Entry = (Get_ELF_Entry_Point() >> 2) | 0x8000000;

   // Fix the addresses
   zx0_bin[1] = (Depacking_Source & 0xff000000) >> 24;
   zx0_bin[0] = (Depacking_Source & 0x00ff0000) >> 16;
   zx0_bin[5] = (Depacking_Source & 0x0000ff00) >> 8;
   zx0_bin[4] = (Depacking_Source & 0x000000ff);
   
   zx0_bin[9] = (Depacking_Dest & 0xff000000) >> 24;
   zx0_bin[8] = (Depacking_Dest & 0x00ff0000) >> 16;
   zx0_bin[13] = (Depacking_Dest & 0x0000ff00) >> 8;
   zx0_bin[12] = (Depacking_Dest & 0x000000ff);

   // Caution: 0xdc is dependant on the depacker code size !!!
   zx0_bin[0xdc] = (Depacked_Entry & 0x000000ff);
   zx0_bin[0xdc + 1] = (Depacked_Entry & 0x0000ff00) >> 8;
   zx0_bin[0xdc + 2] = (Depacked_Entry & 0x00ff0000) >> 16;
   zx0_bin[0xdc + 3] = (Depacked_Entry & 0xff000000) >> 24;

   fwrite(&File_Header, 1, sizeof(File_Header), f_out);
   fwrite(&Section_Header, 1, sizeof(Section_Header), f_out);
   fwrite(zx0_bin, 1, size_zx0_bin, f_out);
   fwrite(pCompressedData, 1, nCompressedSize, f_out);
   fclose(f_out);

   free(pCompressedData);
   free(pDecompressedData);

   if (nOptions & OPT_VERBOSE) {
      double fDelta = ((double)(nEndTime - nStartTime)) / 1000000.0;
      double fSpeed = ((double)nOriginalSize / 1048576.0) / fDelta;
      fprintf(stdout, "\n\rCompressed '%s' in %g seconds, %.02g Mb/s, %d tokens (%g bytes/token), %zu into %zu bytes ==> %g %%\n",
         pszInFilename, fDelta, fSpeed, stats.commands_divisor, (double)nOriginalSize / (double)stats.commands_divisor,
         nOriginalSize, nCompressedSize, (double)(nCompressedSize * 100.0 / nOriginalSize));
   }

   if (nOptions & OPT_STATS) {
      if (stats.literals_divisor > 0)
         fprintf(stdout, "\nLiterals: min: %d avg: %d max: %d count: %d\n", stats.min_literals, stats.total_literals / stats.literals_divisor, stats.max_literals, stats.literals_divisor);
      else
         fprintf(stdout, "\nLiterals: none\n");

      fprintf(stdout, "Normal matches: %d rep matches: %d EOD: %d\n",
         stats.num_normal_matches, stats.num_rep_matches, stats.num_eod);

      if (stats.match_divisor > 0) {
         fprintf(stdout, "Offsets: min: %d avg: %d max: %d count: %d\n", stats.min_offset, (int)(stats.total_offsets / (long long)stats.match_divisor), stats.max_offset, stats.match_divisor);
         fprintf(stdout, "Match lens: min: %d avg: %d max: %d count: %d\n", stats.min_match_len, stats.total_match_lens / stats.match_divisor, stats.max_match_len, stats.match_divisor);
      }
      else {
         fprintf(stdout, "Offsets: none\n");
         fprintf(stdout, "Match lens: none\n");
      }
      if (stats.rle1_divisor > 0) {
         fprintf(stdout, "RLE1 lens: min: %d avg: %d max: %d count: %d\n", stats.min_rle1_len, stats.total_rle1_lens / stats.rle1_divisor, stats.max_rle1_len, stats.rle1_divisor);
      }
      else {
         fprintf(stdout, "RLE1 lens: none\n");
      }
      if (stats.rle2_divisor > 0) {
         fprintf(stdout, "RLE2 lens: min: %d avg: %d max: %d count: %d\n", stats.min_rle2_len, stats.total_rle2_lens / stats.rle2_divisor, stats.max_rle2_len, stats.rle2_divisor);
      }
      else {
         fprintf(stdout, "RLE2 lens: none\n");
      }
      fprintf(stdout, "Safe distance: %d (0x%X)\n", stats.safe_dist, stats.safe_dist);
   }

   u32 Output_Size = sizeof(File_Header) + sizeof(Section_Header) + size_zx0_bin + (u32) nCompressedSize;
   printf("\n");
   printf("     Input     Output       Gain     %%\n");
   printf("--------------------------------------\n");
   printf("%10d %10d", nOriginalSizeSaved, Output_Size);
   printf(" %10d %.02f\n", nOriginalSizeSaved - Output_Size, (((float) (nOriginalSizeSaved - Output_Size)) * 100.0f) / (float) nOriginalSizeSaved);

   return 0;
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv) {
   int i;
   const char *pszInFilename = NULL;
   const char *pszOutFilename = NULL;
   int nArgsError = 0;
   int nCommandDefined = 0;
   char cCommand = 'z';
   unsigned int nOptions = 0;
   unsigned int nMaxWindowSize = 0;

   for (i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "-z")) {
         if (!nCommandDefined) {
            nCommandDefined = 1;
            cCommand = 'z';
         }
         else
            nArgsError = 1;
      }
      else if (!strcmp(argv[i], "-v")) {
         if ((nOptions & OPT_VERBOSE) == 0) {
            nOptions |= OPT_VERBOSE;
         }
         else
            nArgsError = 1;
      }
      else if (!strcmp(argv[i], "-stats")) {
         if ((nOptions & OPT_STATS) == 0) {
            nOptions |= OPT_STATS;
         }
         else
            nArgsError = 1;
      }
      else {
         if (!pszInFilename)
            pszInFilename = argv[i];
         else {
            if (!pszOutFilename)
               pszOutFilename = argv[i];
            else
               nArgsError = 1;
         }
      }
   }

   if (nArgsError || !pszInFilename || !pszOutFilename) {
      fprintf(stderr, "salvadorPS2 command-line tool v" PS2_VERSION " by hitchhikr of Neural\n");
      fprintf(stderr, "Based on salvador command-line tool v" TOOL_VERSION " by Emmanuel Marty\n\n");
      fprintf(stderr, "usage: %s [-v] <infile.elf> <outfile.elf>\n\n", argv[0]);
      fprintf(stderr, "       -stats: show compressed data stats\n");
      fprintf(stderr, "           -v: be verbose\n");
      return 100;
   }

   do_init_time();

   if (cCommand == 'z') {
      int nResult = do_compress(pszInFilename, pszOutFilename, nOptions, nMaxWindowSize);
      return nResult;
   }
   else {
      return 100;
   }
}
