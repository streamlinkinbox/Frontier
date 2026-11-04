/*==========================================================================================================================================
                                                     CONTENTDIRECTORYVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Verify real directory navigation, filtering, confinement, stale-selection removal, and bounded enumeration.

#include "Engine/Interface/ContentDirectory.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool Condition, const char *Description)
{
    if (!Condition)
        throw std::runtime_error(Description);
}
} // namespace

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    namespace fs = std::filesystem;
    try
    {
        Require(ArgumentCount == 2, "Expected disposable evidence directory");
        const auto Evidence = fs::absolute(Arguments[1]);
        fs::remove_all(Evidence);
        const auto Root    = Evidence / "Content";
        const auto Outside = Evidence / "Content-Outside";
        fs::create_directories(Root / "Empty");
        fs::create_directories(Root / "Fonts");
        fs::create_directories(Outside);
        std::ofstream(Root / "Alpha.svg") << "<svg/>";
        std::ofstream(Root / "Beta.PNG") << "raster fixture, metadata only";
        std::ofstream(Root / "Theme.toml") << "theme = 0";
        std::ofstream(Root / "Notes.txt") << "text";
        std::ofstream(Root / "Fonts/Test.TTF") << "font fixture, metadata only";
        const auto UnicodeName = DecodeContentPath("字形 Δ.otf");
        std::ofstream(Root / "Fonts" / UnicodeName) << "font fixture, metadata only";
        ContentDirectory Directory;
        Require(Directory.ConfigureRoot(Root), "Content root rejected");
        Require(Directory.QueryEntries().size() == 6 && Directory.QueryVisible().size() == 6,
                "Unexpected root catalogue");
        Require(Directory.QueryEntries()[0].Name == "Empty" && Directory.QueryEntries()[1].Name == "Fonts",
                "Folders not sorted first");
        Require(!Directory.NavigateUp(), "Up escaped root");
        Require(!Directory.Navigate(Outside) && Directory.QueryDirectory() == fs::canonical(Root),
                "Prefix-similar root escaped confinement");
        Require(!Directory.Navigate("../Content-Outside"), "Relative root escape accepted");
        Require(!Directory.Navigate("Missing") && !Directory.QueryDiagnostic().empty(), "Missing folder not diagnosed");
        Require(!Directory.Navigate("Alpha.svg"), "File treated as directory");
        Require(Directory.Navigate("Fonts"), "Folder navigation failed");
        Require(Directory.QueryEntries().size() == 2 &&
                    Directory.QueryEntries()[0].Category == ContentCategory::Typeface,
                "Typeface classification failed");
        Require(Directory.Select(Directory.QueryEntries()[0].Path), "Real file selection failed");
        Directory.ConfigureFilter(ContentCategory::Typeface, "字形");
        Require(Directory.QueryVisible().size() == 1 && !Directory.QuerySelection(),
                "Unicode search or hidden selection removal failed");
        Require(Directory.Select(Root / "Fonts" / UnicodeName), "Unicode file could not be selected");
        Require(EncodeContentPath(Directory.QuerySelection()->Path.filename()) == "字形 Δ.otf", "UTF-8 path changed");
        Directory.ConfigureFilter(ContentCategory::All, "");
        Require(Directory.NavigateBack() && Directory.QueryDirectory() == fs::canonical(Root),
                "Back navigation failed");
        Require(Directory.NavigateForward() && Directory.QueryDirectory() == fs::canonical(Root / "Fonts"),
                "Forward navigation failed");
        Require(Directory.NavigateUp() && !Directory.QueryCanGoForward(),
                "New navigation retained obsolete forward branch");
        Directory.ConfigureFilter(ContentCategory::VectorImage, "ALPHA");
        Require(Directory.QueryVisible().size() == 1, "Case-insensitive search/category combination failed");
        Require(Directory.Select(Root / "Alpha.svg") && Directory.QuerySelection()->ByteCountKnown &&
                    Directory.QuerySelection()->ByteCount == 6,
                "Real file size or selection failed");
        Require(!Directory.Select(Root / "Beta.PNG") && !Directory.QuerySelection(), "Filtered-out file selected");
        Directory.ConfigureFilter(ContentCategory::RasterImage, "");
        Require(Directory.QueryVisible().size() == 3, "Type filters must retain navigable folders");
        Directory.ConfigureFilter(ContentCategory::Descriptor, "theme");
        Require(Directory.QueryVisible().size() == 1, "Descriptor filter failed");
        Directory.ConfigureFilter(ContentCategory::Other, "notes");
        Require(Directory.QueryVisible().size() == 1, "Other-files classification failed");
        Directory.ConfigureFilter(ContentCategory::All, "");
        Require(Directory.Navigate("Empty") && Directory.QueryVisible().empty() && Directory.QueryDiagnostic().empty(),
                "Empty folder reported as failure");
        Require(Directory.NavigateUp(), "Could not leave empty folder");
        Require(Directory.Select(Root / "Alpha.svg"), "Selection before deletion failed");
        fs::remove(Root / "Alpha.svg");
        Require(Directory.Refresh() && !Directory.QuerySelection(), "Deleted file retained stale selection");
        Require(!Directory.Select(Root / "Alpha.svg"), "Deleted file selected before a refresh");
        std::error_code LinkFailure;
        fs::create_directory_symlink(Outside, Root / "OutsideLink", LinkFailure);
        if (!LinkFailure)
        {
            fs::create_directory_symlink(Root / "Fonts", Root / "InsideLink", LinkFailure);
            Require(!LinkFailure, "Could not create enclosed test link");
            Require(Directory.Refresh() && Directory.QueryOmittedLinks() == 2, "Links not omitted from catalogue");
            Require(!Directory.Navigate("OutsideLink") && !Directory.Navigate("InsideLink"),
                    "Linked directory was followed");
        }
        else
            std::cout << "SKIP: symlink creation unavailable: " << LinkFailure.message() << '\n';
        Require(Directory.Navigate("Empty"), "Missing-directory setup failed");
        fs::remove(Root / "Empty");
        Require(!Directory.Refresh() && Directory.QueryEntries().empty() && !Directory.QueryDiagnostic().empty(),
                "Refresh failure retained stale entries");
        Require(Directory.NavigateBack(), "History could not recover from a removed current folder");
        Require(!Directory.ConfigureRoot(Evidence / "Missing") && Directory.QueryRoot().empty() &&
                    Directory.QueryEntries().empty(),
                "Missing root retained previous catalogue");
        const auto Large = Evidence / "Large";
        fs::create_directories(Large);
        for (size_t Ordinal = 0; Ordinal < ContentDirectory::EntryLimit + 2; ++Ordinal)
            std::ofstream(Large / (std::to_string(Ordinal) + ".toml")) << "";
        Require(Directory.ConfigureRoot(Large) && Directory.QueryEntries().size() == ContentDirectory::EntryLimit &&
                    Directory.QueryTruncated(),
                "Enumeration is unbounded or truncation is hidden");
        fs::remove_all(Evidence);
        std::cout
            << "PASS: real files; folder/history navigation; type/search/Unicode filters; selection; root confinement; "
               "links; empty/missing folders; stale deletion; bounded catalogue\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
