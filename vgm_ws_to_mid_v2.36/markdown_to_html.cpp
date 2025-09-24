#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <regex>
#include <sstream>
#include <algorithm>
#include <cctype>

// Function to escape HTML special characters
std::string escape_html(const std::string& data) {
    std::string buffer;
    buffer.reserve(data.size());
    for(size_t pos = 0; pos != data.size(); ++pos) {
        switch(data[pos]) {
            case '&':  buffer.append("&amp;");       break;
            case '\"': buffer.append("&quot;");      break;
            case '\'': buffer.append("&apos;");      break;
            case '<':  buffer.append("&lt;");        break;
            case '>':  buffer.append("&gt;");        break;
            default:   buffer.append(&data[pos], 1); break;
        }
    }
    return buffer;
}

std::string markdown_to_html(const std::string& markdown) {
    std::string html = "";
    std::stringstream ss(markdown);
    std::string line;
    bool in_table = false;
    bool in_pre = false;
    bool in_ul = false;
    bool in_ol = false;
    std::vector<std::string> table_header;
    bool table_header_processed = false;

    auto close_lists = [&]() {
        if (in_ul) {
            html += "</ul>\n";
            in_ul = false;
        }
        if (in_ol) {
            html += "</ol>\n";
            in_ol = false;
        }
    };

    while (std::getline(ss, line)) {

        // Code blocks
        if (line.substr(0, 3) == "```") {
            close_lists();
            if (in_pre) {
                html += "</code></pre>\n";
                in_pre = false;
            } else {
                std::string lang = line.substr(3);
                html += "<pre><code class=\"language-" + lang + "\">";
                in_pre = true;
            }
            continue;
        }
        if (in_pre) {
            html += escape_html(line) + "\n";
            continue;
        }

        // Headers
        if (line.rfind("#", 0) == 0) {
            close_lists();
            int level = 0;
            while (level < line.length() && line[level] == '#') {
                level++;
            }
            std::string header_content = line.substr(level);
            // trim leading spaces
            header_content.erase(0, header_content.find_first_not_of(" "));

            std::string id = header_content;
            std::transform(id.begin(), id.end(), id.begin(),
                [](unsigned char c){ return std::tolower(c); });
            
            // Replace spaces and special characters with hyphens for ID
            std::regex re_space("[^a-zA-Z0-9]+");
            id = std::regex_replace(id, re_space, "-");
            // Remove leading/trailing hyphens
            id.erase(0, id.find_first_not_of("-"));
            id.erase(id.find_last_not_of("-") + 1);


            html += "<h" + std::to_string(level) + " id=\"" + id + "\">" + header_content + "</h" + std::to_string(level) + ">\n";
            continue;
        }
        
        // Horizontal Rule
        if (line.find("---") == 0) {
            close_lists();
            html += "<hr>\n";
            continue;
        }

        // Tables
        if (line.find("|") != std::string::npos) {
            close_lists();
            
            // Trim leading/trailing spaces
            line.erase(0, line.find_first_not_of(" \t"));
            line.erase(line.find_last_not_of(" \t") + 1);

            if (line.length() > 0 && line[0] == '|') {
                std::stringstream line_ss(line.substr(1, line.length() - (line.back() == '|' ? 2 : 1)));
                std::string segment;
                std::vector<std::string> seglist;
                while(std::getline(line_ss, segment, '|')) {
                    // trim spaces from segment
                    segment.erase(0, segment.find_first_not_of(" \t"));
                    segment.erase(segment.find_last_not_of(" \t") + 1);
                    seglist.push_back(segment);
                }

                // Check for header separator
                if (seglist.size() > 0 && std::all_of(seglist[0].begin(), seglist[0].end(), [](char c){ return c == '-' || c == ' ' || c == ':'; })) {
                    if (!table_header.empty()) {
                        html += "<thead>\n<tr>\n";
                        for(const auto& header : table_header) {
                            html += "<th>" + header + "</th>\n";
                        }
                        html += "</tr>\n</thead>\n<tbody>\n";
                        table_header.clear();
                        table_header_processed = true;
                    }
                } else {
                    if (!in_table) {
                        html += "<table>\n";
                        in_table = true;
                        table_header_processed = false;
                    }
                    
                    if (!table_header_processed) {
                         table_header = seglist;
                    } else {
                        html += "<tr>\n";
                        for(const auto& cell : seglist) {
                            html += "<td>" + cell + "</td>\n";
                        }
                        html += "</tr>\n";
                    }
                }
                continue;
            }
        }
        
        if (in_table) {
            html += "</tbody>\n</table>\n";
            in_table = false;
            table_header.clear();
            table_header_processed = false;
        }

        // Lists
        size_t first_char_pos = line.find_first_not_of(" \t");
        if (first_char_pos != std::string::npos) {
            if (line.substr(first_char_pos, 2) == "* " || line.substr(first_char_pos, 2) == "- ") {
                if (!in_ul) {
                    close_lists();
                    html += "<ul>\n";
                    in_ul = true;
                }
                html += "<li>" + line.substr(first_char_pos + 2) + "</li>\n";
                continue;
            } else if (isdigit(line[first_char_pos])) {
                size_t dot_pos = line.find(". ");
                if (dot_pos != std::string::npos) {
                    if (!in_ol) {
                        close_lists();
                        html += "<ol>\n";
                        in_ol = true;
                    }
                    html += "<li>" + line.substr(dot_pos + 2) + "</li>\n";
                    continue;
                }
            }
        }
        
        close_lists();

        // Paragraphs
        if (!line.empty()) {
            html += "<p>" + line + "</p>\n";
        }
    }
    close_lists();
    if (in_table) {
        html += "</tbody>\n</table>\n";
    }

    // Post-process for inline elements
    std::regex bold_re("\\*\\*(.*?)\\*\\*");
    html = std::regex_replace(html, bold_re, "<strong>$1</strong>");

    std::regex italic_re("\\*(.*?)\\*");
    html = std::regex_replace(html, italic_re, "<em>$1</em>");

    std::regex code_re("`(.*?)`");
    html = std::regex_replace(html, code_re, "<code>$1</code>");

    std::regex link_re("\\[([^\\]]+)\\]\\(([^\\)]+)\\)");
    html = std::regex_replace(html, link_re, "<a href=\"$2\">$1</a>");

    return html;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <input_markdown> <output_html>" << std::endl;
        return 1;
    }

    std::ifstream md_file(argv[1]);
    if (!md_file) {
        std::cerr << "Error: Cannot open input file " << argv[1] << std::endl;
        return 1;
    }

    std::string md_content((std::istreambuf_iterator<char>(md_file)),
                             std::istreambuf_iterator<char>());

    std::string title = "Documentation";
    std::string temp_line;
    std::stringstream ss(md_content);
    if(std::getline(ss, temp_line)){
        if(temp_line.rfind("# ", 0) == 0){
            title = temp_line.substr(2);
        }
    }
    
    std::string body_html = markdown_to_html(md_content);

    std::ofstream html_file(argv[2]);
    if (!html_file) {
        std::cerr << "Error: Cannot open output file " << argv[2] << std::endl;
        return 1;
    }

    html_file << R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>)" << title << R"(</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Helvetica, Arial, sans-serif, "Apple Color Emoji", "Segoe UI Emoji";
            line-height: 1.6;
            color: #24292e;
            max-width: 800px;
            margin: 0 auto;
            padding: 30px;
        }
        h1, h2, h3, h4, h5, h6 {
            margin-top: 24px;
            margin-bottom: 16px;
            font-weight: 600;
            line-height: 1.25;
        }
        h1 { font-size: 2em; border-bottom: 1px solid #eaecef; padding-bottom: .3em; }
        h2 { font-size: 1.5em; border-bottom: 1px solid #eaecef; padding-bottom: .3em; }
        h3 { font-size: 1.25em; }
        h4 { font-size: 1em; }
        a { color: #0366d6; text-decoration: none; }
        a:hover { text-decoration: underline; }
        code {
            padding: .2em .4em;
            margin: 0;
            font-size: 85%;
            background-color: rgba(27,31,35,.05);
            border-radius: 3px;
            font-family: "SFMono-Regular", Consolas, "Liberation Mono", Menlo, Courier, monospace;
        }
        pre {
            padding: 16px;
            overflow: auto;
            font-size: 85%;
            line-height: 1.45;
            background-color: #f6f8fa;
            border-radius: 3px;
        }
        pre code {
            display: inline;
            padding: 0;
            margin: 0;
            overflow: visible;
            line-height: inherit;
            word-wrap: normal;
            background-color: transparent;
            border: 0;
        }
        table {
            border-collapse: collapse;
            width: 100%;
            margin-top: 1em;
            margin-bottom: 1em;
        }
        th, td {
            border: 1px solid #dfe2e5;
            padding: 6px 13px;
        }
        th {
            font-weight: 600;
            background-color: #f6f8fa;
        }
        ul, ol { padding-left: 2em; }
        li { margin-top: .25em; }
        blockquote {
            margin-left: 0;
            padding-left: 1em;
            color: #6a737d;
            border-left: .25em solid #dfe2e5;
        }
    </style>
</head>
<body>
)" << body_html << R"(</body>
</html>
)";

    return 0;
}
