// Copyright Jianing Yang <jianingy.yang@gmail.com> 2009

#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <math.h>
#include "trie_impl.h"

#define length(x) (strlen(x))
#define unsigned_value(x, y) (unsigned int)(j + 1)
#define signed_value(x, y) (int)(3 - j)

using namespace dutil;

int main(int argc, char *argv[])
{
	if (argc < 2) {
		std::cout << argv[0] << " trie_type(1 = single, * = double) " << std::endl;
		return 0;
	}

	trie *trie = trie::create_trie(argv[1][0] == '1'?trie::SINGLE_TRIE:trie::DOUBLE_TRIE);
	size_t i;
	const char prefix[] = "back!";
	const char *dict[] = {"bachelor", "back", "badge", "badger", "badness", "bcs", "backbone"};
	trie::key_type key;

	for (i = 0; i < sizeof(dict) / sizeof(char *); i++) {
		key.assign(dict[i], strlen(dict[i]));
		trie->insert(key, i + 1);
	}

	trie::result_type result;
	for (i = 0; i < sizeof(prefix) / sizeof(prefix[0]); i++) {
		trie::key_type store(prefix, i);
		std::cout << "== Searching " << store.c_str() << " == " << std::endl;
		result.clear();
		trie->prefix_search(store, &result);
		trie::result_type::const_iterator it;
		for (it = result.begin(); it != result.end(); it++)
		std::cout << "size:"<<it->first.size()<<" "<<"length:"<<length(it->first.c_str())<<" "<<it->first.c_str() << " = " << it->second << std::endl;
	}
	std::cout << "== Done ==" << std::endl;
	delete trie;

}
