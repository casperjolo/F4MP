#pragma once

namespace f4mp::client::console
{
	// Installs the "f4mp" console command (help, status, connect, disconnect, say, list, name)
	// by taking over the table entry of an unused stock debug command. Needs the Address
	// Library, so call it after F4SE::Init.
	bool Register();

	// Runs one command line such as "f4mp say hello" (the leading "f4mp" is optional).
	void Execute(std::string_view a_line);
}
