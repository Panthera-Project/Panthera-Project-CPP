//////////////////////////////////////////////////////////////////////
//                                                                  //
// Part of the PCIT-CPP, under the Apache License v2.0              //
// You may not use this file except in compliance with the License. //
// See `http://www.apache.org/licenses/LICENSE-2.0` for info        //
//                                                                  //
//////////////////////////////////////////////////////////////////////


#pragma once

#include <deque>

#include <Evo.h>
#include <PCIT_core.h>

#include "./Token.h"

namespace pcit::panther{


	class TokenBuffer{
		public:
			TokenBuffer() = default;
			~TokenBuffer() = default;

			TokenBuffer(const TokenBuffer& rhs) = delete;
			
			TokenBuffer(TokenBuffer&& rhs) noexcept 
				: tokens(std::move(rhs.tokens)),
				  string_literals(std::move(rhs.string_literals)),
				  is_locked(rhs.is_locked)
				{};


			auto createToken(Token::Kind kind, Token::Location location) noexcept -> Token::ID;
			auto createToken(Token::Kind kind, Token::Location location, bool value) noexcept -> Token::ID;
			auto createToken(Token::Kind kind, Token::Location location, uint64_t value) noexcept -> Token::ID;
			auto createToken(Token::Kind kind, Token::Location location, float64_t value) noexcept -> Token::ID;
			auto createToken(Token::Kind kind, Token::Location location, std::string&& value) noexcept -> Token::ID;

			EVO_NODISCARD auto get(Token::ID id) const noexcept -> const Token&;
			EVO_NODISCARD auto get(Token::ID id)       noexcept ->       Token&;

			EVO_NODISCARD auto operator[](Token::ID id) const noexcept -> const Token& { return this->get(id); };
			EVO_NODISCARD auto operator[](Token::ID id)       noexcept ->       Token& { return this->get(id); };

			EVO_NODISCARD auto size() const noexcept -> size_t { return this->tokens.size(); };

			EVO_NODISCARD auto begin() const noexcept -> Token::ID::Iterator {
				return Token::ID::Iterator(Token::ID(0));
			};

			EVO_NODISCARD auto end() const noexcept -> Token::ID::Iterator {
				return Token::ID::Iterator(Token::ID(uint32_t(this->tokens.size())));
			};


			auto lock() noexcept -> void { this->is_locked = true; };
			EVO_NODISCARD auto isLocked() const noexcept -> bool { return this->is_locked; };

		
		private:
			std::vector<Token> tokens{};
			std::vector<std::unique_ptr<std::string>> string_literals{};
			bool is_locked = false;
	};


};
