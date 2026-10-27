/* bank_main.c -- the port's sample coder (src/kn7/bank.c) on the PC: reads a bank image, writes its records and prints
 * the checksum. tests/bank/check_bank.py compares the records with technics-anotherworld's dsp_bank.py. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

uint32_t bankEncode(const uint8_t *bank, uint32_t words, uint32_t *out);
uint32_t bankEncodeMax(uint32_t words);
uint32_t bankChecksum(const uint8_t *bank, uint32_t words);

int main(int argc, char **argv)
{
	if (argc != 3)
		return 2;
	FILE *f = fopen(argv[1], "rb");
	if (!f)
		return 1;
	fseek(f, 0, SEEK_END);
	const long n = ftell(f);
	rewind(f);
	uint8_t *bank = malloc(n);
	if (fread(bank, 1, n, f) != (size_t)n)
		return 1;
	fclose(f);
	const uint32_t words = (uint32_t)n / 4;
	uint32_t *out = malloc(bankEncodeMax(words));
	const uint32_t bytes = bankEncode(bank, words, out);
	f = fopen(argv[2], "wb");
	fwrite(out, 1, bytes, f);
	fclose(f);
	printf("%u %u %08x\n", words, bytes, bankChecksum(bank, words));
	return 0;
}
