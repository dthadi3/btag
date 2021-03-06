/*
 * This file is part of btag.
 *
 * © 2010 Fernando Tarlá Cardoso Lemos
 *
 * Refer to the LICENSE file for licensing information.
 *
 */

#ifndef INTERACTIVE_TERMINAL_H
#define INTERACTIVE_TERMINAL_H

#include <boost/optional.hpp>
#include <string>

class InteractiveTerminal
{
    public:
        template<typename T> class Validator
        {
            public:
                virtual bool validate(const T &, boost::optional<std::wstring> &error_message) const = 0;
        };

        virtual bool ask_yes_no_question(const std::wstring &question,
                const boost::optional<bool> &default_answer = boost::optional<bool>()) = 0;
        virtual std::wstring ask_string_question(const std::wstring &question,
                const boost::optional<std::wstring> &default_answer = boost::optional<std::wstring>(),
                const Validator<std::wstring> *validator = NULL) = 0;
        virtual int ask_number_question(const std::wstring &question,
                const boost::optional<int> &default_answer = boost::optional<int>(),
                const Validator<int> *validator = NULL) = 0;

        virtual void display_info_message(const std::string &message) = 0;
        virtual void display_info_message(const std::wstring &message) = 0;
        virtual void display_warning_message(const std::string &message) = 0;
        virtual void display_warning_message(const std::wstring &message) = 0;
};

#endif
