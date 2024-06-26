//////////////////////////////////////////////////////////////////////
//                                                                  //
// Part of the PCIT-CPP, under the Apache License v2.0              //
// You may not use this file except in compliance with the License. //
// See `http://www.apache.org/licenses/LICENSE-2.0` for info        //
//                                                                  //
//////////////////////////////////////////////////////////////////////


#include "../include/default_diagnostic_callback.h"

namespace pcit::panther{


	static auto print_location(
		const core::Printer& printer, const Source& source, Diagnostic::Level level, const Source::Location& location
	) noexcept -> void {

		///////////////////////////////////
		// print file location

		printer.printGray(
			std::format(
				"\t{}:{}:{}\n",
				source.getLocationAsString(),
				location.lineStart,
				location.collumnStart
			)
		);

		const std::string line_number_str = std::to_string(location.lineStart);


		///////////////////////////////////
		// find line in the source code

		size_t cursor = 0;
		size_t current_line = 1;
		while(current_line < location.lineStart){
			evo::debugAssert(
				cursor < source.getData().size(), "out of bounds looking for line in source code for error"
			);

			if(source.getData()[cursor] == '\n'){
				current_line += 1;

			}else if(source.getData()[cursor] == '\r'){
				current_line += 1;

				if(source.getData()[cursor + 1] == '\n'){
					cursor += 1;
				}
			}

			cursor += 1;
		};


		///////////////////////////////////
		// get actual line and remove leading whitespace

		auto line_str = std::string{};
		size_t point_collumn = location.collumnStart;
		bool remove_whitespace = true;

		while(source.getData()[cursor] != '\n' && source.getData()[cursor] != '\r' && cursor < source.getData().size()){
			if(remove_whitespace && (source.getData()[cursor] == '\t' || source.getData()[cursor] == ' ')){
				// remove leading whitespace
				point_collumn -= 1;

			}else{
				line_str += source.getData()[cursor];
				remove_whitespace = false;
			}

			cursor += 1;
		};

		printer.printGray(std::format("\t{} | {}\n", line_number_str, line_str));


		///////////////////////////////////
		// print formatting space for pointer line

		auto line_space_str = std::string();
		for(size_t i = 0; i < line_number_str.size(); i+=1){
			line_space_str += ' ';
		}
		printer.printGray(std::format("\t{} | ", line_space_str));


		///////////////////////////////////
		// print pointer str

		auto pointer_str = std::string();

		for(size_t i = 0; i < point_collumn - 1; i+=1){
			pointer_str += ' ';
		}

		if(location.lineStart == location.lineEnd){
			for(uint32_t i = location.collumnStart; i < location.collumnEnd + 1; i+=1){
				pointer_str += '^';
			}
		}else{
			for(size_t i = point_collumn; i < line_str.size() + 1; i+=1){
				if(i == point_collumn){
					pointer_str += '^';
				}else{
					pointer_str += '~';
				}
			}
		}

		pointer_str += '\n';

		switch(level){
			break; case Diagnostic::Level::Fatal:   printer.printError(pointer_str);
			break; case Diagnostic::Level::Error:   printer.printError(pointer_str);
			break; case Diagnostic::Level::Warning: printer.printWarning(pointer_str);
			break; case Diagnostic::Level::Info:    printer.printInfo(pointer_str);
		};
	};


	

	auto createDefaultDiagnosticCallback(const core::Printer& printer_ref) noexcept -> Context::DiagnosticCallback {
		return [&printer = printer_ref](const Context& context, const Diagnostic& diagnostic) noexcept -> void {

			const std::string diagnostic_message = std::format(
				"<{}|{}> {}\n", diagnostic.level, diagnostic.code, diagnostic.message
			);

			switch(diagnostic.level){
				break; case Diagnostic::Level::Fatal:   printer.printFatal(diagnostic_message);
				break; case Diagnostic::Level::Error:   printer.printError(diagnostic_message);
				break; case Diagnostic::Level::Warning: printer.printWarning(diagnostic_message);
			};

			if(diagnostic.location.has_value()){
				const Source::Location& location = *diagnostic.location;
				const Source& source = context.getSourceManager().getSource(location.sourceID);

				print_location(printer, source, diagnostic.level, location);
			}

			for(const Diagnostic::Info& info : diagnostic.infos){
				printer.printCyan(std::format("\t<Info> {}\n", info.message));
				if(info.location.has_value()){
					const Source& source = context.getSourceManager().getSource(info.location->sourceID);
					print_location(printer, source, Diagnostic::Level::Info, *info.location);
				}
			}
		};
	};


};