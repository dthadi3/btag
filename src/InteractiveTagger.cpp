/*
 * This file is part of btag.
 *
 * © 2010 - 2013, 2015 Fernando Tarlá Cardoso Lemos
 *
 * Refer to the LICENSE file for licensing information.
 *
 */

#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <cassert>
#include <cstdlib>
#include <exception>

#include "InteractiveTagger.h"
#include "number_cast.h"
#include "validators.h"
#include "wide_string_cast.h"

namespace fs = boost::filesystem;

InteractiveTagger::InteractiveTagger()
    : m_input_filter(NULL), m_output_filter(NULL),
      m_dry_run(false), m_ask_track(false)
{
    // Load the extensions supported by TagLib
    TagLib::StringList extensions = TagLib::FileRef::defaultFileExtensions();
    for (TagLib::StringList::ConstIterator it = extensions.begin(); it != extensions.end(); ++it)
        m_supported_extensions.push_back(L'.' + it->toWString());
}

void InteractiveTagger::set_file_rename_format(const std::string &format)
{
    m_file_rename_format = boost::lexical_cast<std::wstring>(format);
}

void InteractiveTagger::set_dir_rename_format(const std::string &format)
{
    m_dir_rename_format = boost::lexical_cast<std::wstring>(format);
}

#ifdef CUESHEET_SUPPORT
void InteractiveTagger::load_cue_sheets(const std::list<std::string> &filenames, const std::string &encoding)
{
    try {
        m_cue.reset(new CueReaderMultiplexer(filenames, encoding));
    }
    catch (std::exception &e) {
        m_terminal->display_warning_message(e.what());
        m_terminal->display_warning_message("Ignoring CUE files");
    }
}
#endif

void InteractiveTagger::tag(int num_paths, const char **paths)
{
    assert(m_terminal);

    // Build a list with the normalized paths
    std::list<fs::path> path_list;
    for (int i = 0; i < num_paths; ++i) {
        char *real_path = realpath(paths[i], NULL);
        if (!real_path) {
            std::string errstr("\"");
            errstr += paths[i];
            errstr += "\" is not a valid path";
            m_terminal->display_warning_message(errstr);
            continue;
        }
        path_list.push_back(fs::path(real_path));
        free(real_path);
    }

    // Sort it and tag the paths
    path_list.sort();
    BOOST_FOREACH(const fs::path &path, path_list) {
        // Check if the path exists
        if (!fs::exists(path)) {
            m_terminal->display_warning_message(L"Path \"" + path.string<std::wstring>()
                    + L"\" not found, skipping...");
            continue;
        }

        try {
            // If it's a regular file, just tag it
            if (fs::is_regular_file(path)) {
                if (!is_supported_extension(path)) {
                    m_terminal->display_warning_message(L"Path \"" + path.string<std::wstring>()
                            + L"\" has no supported extension, skipping...");
                    continue;
                }
                tag_file(path);
            }

            // If it's a directory, tag its contents
            else if (fs::is_directory(path)) {
                tag_directory(path);
            }

            // It's none of the above, do something about it
            else {
                m_terminal->display_warning_message(L"Path \"" + path.string<std::wstring>()
                        + L"\" is not a regular file or directory, skipping...");
            }
        }
        catch (std::exception &e) {
            m_terminal->display_warning_message(e.what());
        }
    }

    // Save the unsaved files
    if (!m_unsaved_files.empty()
            && (m_dry_run || m_terminal->ask_yes_no_question(L"=== OK to save the changes to the files"))) {
        if (m_dry_run)
            m_terminal->display_info_message("=== Not saving changes (dry run mode)");
        BOOST_FOREACH(TagLib::FileRef &f, m_unsaved_files) {
            std::string message(m_dry_run ? "Not saving" : "Saving");
            message += " \"" + std::string(f.file()->name()) + '\"';
            m_terminal->display_info_message(message);
            if (!m_dry_run && !f.save())
                m_terminal->display_warning_message("Unable to save " + std::string(f.file()->name()) + "\"");
        }
    }

    // Perform the pending renames
    if (!m_pending_renames.empty()
            && (m_dry_run || m_terminal->ask_yes_no_question(L"=== OK to rename the files"))) {
        if (m_dry_run)
            m_terminal->display_info_message("=== Not renaming files (dry run mode)");
        std::list<std::pair<fs::path, fs::path> >::const_iterator it;
        for (it = m_pending_renames.begin(); it != m_pending_renames.end(); ++it) {
            const fs::path &from((*it).first);
            const fs::path &to((*it).second);
            m_terminal->display_info_message(from.string());
            m_terminal->display_info_message(L"-> " + to.string<std::wstring>());
            if (!m_dry_run) {
                try { fs::rename(from, to); }
                catch (std::exception &e) { m_terminal->display_warning_message(e.what()); }
            }
        }
    }

    m_terminal->display_info_message("=== All done!");
}

bool InteractiveTagger::is_supported_extension(const fs::path &path)
{
    std::wstring extension(path.extension().string<std::wstring>());
    boost::to_lower(extension);

    BOOST_FOREACH(const std::wstring &supported_extension, m_supported_extensions) {
        if (extension == supported_extension)
            return true;
    }

    return false;
}

std::wstring InteractiveTagger::replace_tokens(const std::wstring &str,
        const std::map<std::wstring, std::wstring> &replacements)
{
    std::wstring res;
    res.reserve(str.size() * 3);

    bool parsing_token = false;
    std::wstring current_token;
    BOOST_FOREACH(char c, str) {
        if (c == '%') {
            if (parsing_token) {
                std::map<std::wstring, std::wstring>::const_iterator it = replacements.find(current_token);
                res += it == replacements.end() ? current_token : it->second;
                current_token.erase();
            }
            parsing_token = true;
        }
        else if (parsing_token) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                current_token += c;
            }
            else {
                std::map<std::wstring, std::wstring>::const_iterator it = replacements.find(current_token);
                res += it == replacements.end() ? current_token : it->second;
                res += c;
                parsing_token = false;
                current_token.erase();
            }
        }
        else {
            res += c;
        }
    }
    if (parsing_token) {
        std::map<std::wstring, std::wstring>::const_iterator it = replacements.find(current_token);
        res += it == replacements.end() ? current_token : it->second;
    }

    return res;
}

void InteractiveTagger::tag_file(const fs::path &path, ConfirmationHandler &artist_confirmation,
       ConfirmationHandler &album_confirmation, int *year, int track)
{
    m_terminal->display_info_message(L"=== Tagging \"" + path.filename().string<std::wstring>() + L"\"");

    // Get a reference to the file
    TagLib::FileRef f(path.c_str());

    // Ask for the track
    bool ask_track = m_ask_track;
    TrackValidator track_validator;
    if (track == -1) {
        track = f.tag()->track();
        boost::optional<std::wstring> error_message;
        if (!track_validator.validate(track, error_message)) {
            track = 0;
            ask_track = true;
        }
    }
    if (ask_track) {
        track = m_terminal->ask_number_question(L"Track",
                track > 0 ? track : boost::optional<int>(), &track_validator);
    }
    f.tag()->setTrack(track);

    // Ask for the artist
    artist_confirmation.reset();
    bool set_artist = false;
#ifdef CUESHEET_SUPPORT
    if (m_cue) {
        boost::optional<std::wstring> artist = m_cue->artist_for_track(track);
        if (artist) {
            artist_confirmation.set_local_default(m_input_filter->filter(*artist));
            set_artist = true;
        }
    }
#endif
    if (!set_artist && !f.tag()->artist().isNull())
        artist_confirmation.set_local_default(m_input_filter->filter(f.tag()->artist().toWString()));
    artist_confirmation.ask(L"Artist");
    while (!artist_confirmation.complies())
        artist_confirmation.ask(L"Artist (confirmation)");
    f.tag()->setArtist(artist_confirmation.answer());

    // Ask for the album
    album_confirmation.reset();
    bool set_album = false;
#ifdef CUESHEET_SUPPORT
    if (m_cue) {
        boost::optional<std::wstring> album = m_cue->album();
        if (album) {
            album_confirmation.set_local_default(m_input_filter->filter(*album));
            set_album = true;
        }
    }
#endif
    if (!set_album && !f.tag()->album().isNull())
        album_confirmation.set_local_default(m_input_filter->filter(f.tag()->album().toWString()));
    album_confirmation.ask(L"Album");
    while (!album_confirmation.complies())
        album_confirmation.ask(L"Album (confirmation)");
    f.tag()->setAlbum(album_confirmation.answer());

    // Ask for the year
    boost::optional<int> default_year;
    if (year && *year != -1)
        default_year = *year;
#ifdef CUESHEET_SUPPORT
    else {
        boost::optional<int> cue_year;
        if (m_cue) {
            cue_year = m_cue->year();
            if (cue_year)
                default_year = *cue_year;
        }
        if (!cue_year && f.tag()->year())
            default_year = f.tag()->year();
    }
#else
    else if (f.tag()->year())
        default_year = f.tag()->year();
#endif
    YearValidator year_validator;
    int new_year = m_terminal->ask_number_question(L"Year", default_year, &year_validator);
    f.tag()->setYear(new_year);
    if (year) *year = new_year;

    // Display the track number closer to the song title
    if (!ask_track)
        m_terminal->display_info_message(L"Track: " + number_cast(track));

    // Ask for the song title
    ConfirmationHandler title_confirmation(*m_terminal, m_input_filter, m_output_filter);
    title_confirmation.reset();
    bool set_title = false;
#ifdef CUESHEET_SUPPORT
    if (m_cue) {
        boost::optional<std::wstring> title = m_cue->title_for_track(track);
        if (title) {
            title_confirmation.set_local_default(m_input_filter->filter(*title));
            set_title = true;
        }
    }
#endif
    if (!set_title && !f.tag()->title().isNull())
        title_confirmation.set_local_default(m_input_filter->filter(f.tag()->title().toWString()));
    title_confirmation.ask(L"Title");
    while (!title_confirmation.complies())
        title_confirmation.ask(L"Title (confirmation)");
    f.tag()->setTitle(title_confirmation.answer());

    // Reset the comment and genre fields
    f.tag()->setComment(TagLib::String::null);
    f.tag()->setGenre(TagLib::String::null);

    // Add it to the list of unsaved files
    m_unsaved_files.push_back(f);

    // Add it to the list of pending renames based on the supplied format
    if (m_file_rename_format) {
        std::map<std::wstring, std::wstring> tokens;
        tokens[L"artist"] = m_renaming_filter->filter(artist_confirmation.answer());
        tokens[L"album"] = m_renaming_filter->filter(album_confirmation.answer());
        tokens[L"year"] = number_cast(new_year);
        std::wstring track_str(number_cast(track));
        if (track_str.size() == 1) track_str = L"0" + track_str;
        tokens[L"track"] = track_str;
        tokens[L"title"] = m_renaming_filter->filter(title_confirmation.answer());
        fs::path new_path = path.parent_path();
        new_path /= replace_tokens(*m_file_rename_format, tokens)
            + boost::to_lower_copy(path.extension().string<std::wstring>());
        if (new_path != path)
            m_pending_renames.push_back(std::pair<fs::path, fs::path>(path, new_path));
    }
}

void InteractiveTagger::tag_file(const boost::filesystem::path &path)
{
    // Tag a file without recording the global default
    ConfirmationHandler amnesiac_artist(*m_terminal, m_input_filter, m_output_filter);
    ConfirmationHandler amnesiac_album(*m_terminal, m_input_filter, m_output_filter);
    tag_file(path, amnesiac_artist, amnesiac_album, NULL, -1);
}

void InteractiveTagger::tag_directory(const fs::path &path)
{
    m_terminal->display_info_message(L"=== Entering \"" + path.string<std::wstring>() + L"\"");

    // Create lists of files and subdirectories
    std::list<fs::path> file_list, dir_list;
    fs::directory_iterator end_it;
    for (fs::directory_iterator it(path); it != end_it; ++it) {
        // Normalize the path (needed here so that we follow symlinks)
        char *real_path = realpath(it->path().c_str(), NULL);
        if (!real_path) {
            m_terminal->display_warning_message(L"\"" + it->path().filename().string<std::wstring>()
                    + L"\" is not a valid path, skipping...");
            continue;
        }
        fs::path boost_path(real_path);
        free(real_path);

        // Add to the right list
        if (fs::is_regular_file(boost_path)) {
            if (is_supported_extension(boost_path)) {
                file_list.push_back(boost_path);
            }
            else {
                m_terminal->display_warning_message(L"\"" + it->path().filename().string<std::wstring>()
                        + L"\" has no supported extension, skipping...");
            }
        }
        else if (fs::is_directory(boost_path)) {
            dir_list.push_back(boost_path);
        }
        else {
            m_terminal->display_warning_message(L"\"" + boost_path.filename().string<std::wstring>()
                    + L"\" is not a regular file or directory, skipping...");
        }
    }

    // Sort those lists
    file_list.sort();
    dir_list.sort();

    // Tag all individual files
    ConfirmationHandler artist(*m_terminal, m_input_filter, m_output_filter);
    ConfirmationHandler album(*m_terminal, m_input_filter, m_output_filter);
    int year = -1;
    if (!file_list.empty()) {
        int track = 1;
        BOOST_FOREACH(const fs::path &p, file_list)
            tag_file(p, artist, album, &year, track++);

#ifdef CUESHEET_SUPPORT
        // Done using the CUE file, if any
        m_cue.reset(NULL);
#endif
    }

    // We'll ask confirmation to descend into the subdirectories only if there are files
    BOOST_FOREACH(const fs::path &p, dir_list) {
        if (file_list.empty() || m_terminal->ask_yes_no_question(L"Descend into subdirectory \""
                    + boost::lexical_cast<std::wstring>(p.filename()) + L'"', false))
            tag_directory(p);
    }

    // Add it to the list of pending renames based on the supplied format
    if (!artist.answer().empty() && m_dir_rename_format) {
        std::map<std::wstring, std::wstring> tokens;
        tokens[L"artist"] = m_renaming_filter->filter(artist.answer());
        tokens[L"album"] = m_renaming_filter->filter(album.answer());
        tokens[L"year"] = number_cast(year);
        fs::path new_path = path.parent_path();
        new_path /= replace_tokens(*m_dir_rename_format, tokens);
        if (new_path != path)
            m_pending_renames.push_back(std::pair<fs::path, fs::path>(path, new_path));
    }
}
