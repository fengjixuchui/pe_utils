#include <Windows.h>
#include <iostream>
#include <fstream>

#include <peconv.h> // include libPeConv header
#include "util.h"

bool isSyscallFunc(const std::string &funcName)
{
	std::string prefix("Nt");
	if (funcName.size() < (prefix.size() + 1)) {
		return false;
	}
	if (funcName.compare(0, prefix.size(), prefix) != 0) {
		return false;
	}
	char afterPrefix = funcName.at(prefix.size());
	if (afterPrefix >= 'A' && afterPrefix <= 'Z') {
		// the name of the function after the Nt prefix will start in uppercase,
		// syscalls are in functions like: NtUserSetWindowLongPtr, but not: NtdllDefWindowProc_A
		return true;
	}
	return false;
}

size_t extract_syscalls(BYTE* pe_buf, size_t pe_size, std::stringstream& outs, size_t startID = 0)
{
	std::vector<std::string> names_list;
	if (!peconv::get_exported_names(pe_buf, names_list)) {
		return 0;
	}

	std::map<DWORD, std::string> sys_functions;
	for (auto itr = names_list.begin(); itr != names_list.end(); ++itr) {
		std::string funcName = *itr;
		if (isSyscallFunc(funcName)) {
			ULONG_PTR va = (ULONG_PTR)peconv::get_exported_func(pe_buf, funcName.c_str());
			if (!va) continue;

			DWORD rva = DWORD(va - (ULONG_PTR)pe_buf);
			sys_functions[rva] = funcName;
		}
	}
	size_t id = startID;
	for (auto itr = sys_functions.begin(); itr != sys_functions.end(); ++itr) {
		std::string funcName = itr->second;
		outs << std::hex << "0x" << id++ << "," << funcName << "\n";
	}
	return id;
}

size_t extract_from_dll(IN const std::string &path, size_t startSyscallID, OUT std::stringstream &outs)
{
	size_t bufsize = 0;
	BYTE* buffer = peconv::load_pe_module(path.c_str(), bufsize, false, false);

	if (!buffer) {
		std::cerr << "Failed to load the PE: " << path << "\n";
		return 0;
	}

	size_t extracted_count = extract_syscalls(buffer, bufsize, outs, startSyscallID);
	peconv::free_pe_buffer(buffer);

	if (!extracted_count) {
		std::cerr << "No syscalls extracted from: " << path << "\n";
	}
	return extracted_count;
}

int main(int argc, char *argv[])
{
	std::string outFileName = "syscalls.txt";
	if (argc < 2) {
		std::cout << "Extract syscalls from system DLLs (ntdll.dll, win32u.dll)\n"
			<< "Source: https://github.com/hasherezade/pe_utils\n"
			<< "\tOptional arg: <out path>"
			<< std::endl;
	}
	else {
		outFileName = argv[1];
	}

	PVOID old_val = NULL;
	util::wow64_disable_fs_redirection(&old_val);

	std::stringstream outs;
	size_t extracted_count = 0;

	char ntdll_path[MAX_PATH] = { 0 };
	ExpandEnvironmentStringsA("%SystemRoot%\\system32\\ntdll.dll", ntdll_path, MAX_PATH);
	extracted_count += extract_from_dll(ntdll_path, 0, outs);

	char win32u_path[MAX_PATH] = { 0 };
	ExpandEnvironmentStringsA("%SystemRoot%\\system32\\win32u.dll", win32u_path, MAX_PATH);
	extracted_count += extract_from_dll(win32u_path, 0x1000, outs);

	util::wow64_revert_fs_redirection(&old_val);

	if (!extracted_count) {
		std::cerr << "Failed to extract syscalls.\n";
		return (-1);
	}

	std::ofstream myfile;
	myfile.open(outFileName);
	myfile << outs.str();
	myfile.close();
	std::cout << "Saved to: " << outFileName << std::endl;
	return 0;
}
