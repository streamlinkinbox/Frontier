/*====================================================================================================================================
                                                         SOLIDARCFILEDIALOG.H
====================================================================================================================================*/
// 🧩 Native file picker for Construct › Image plane (kept apart so windows.h never meets the interface headers)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <string>

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// The picture the user picks in the system's Open dialog (UTF-8 path), empty when cancelled or where no dialog exists
// (Linux / headless builds type the path instead).
[[nodiscard]] std::string BrowseSolidArcPicture();

// True where BrowseSolidArcPicture shows a dialog (Windows).
[[nodiscard]] bool SolidArcFileDialogAvailableCondition() noexcept;
