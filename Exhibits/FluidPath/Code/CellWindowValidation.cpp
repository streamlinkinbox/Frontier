#include "../../../Engine/Fluid/SpatialGrid/CellWindow.h"

#include <iostream>
#include <stdexcept>

int main()
{
    using namespace Frontier::Fluid;
    PositionPair Focus{};
    CellPair Extent{}, Origin{};
    CellTriple Cell{}, WrapExtent{};
    double Edge;
    int Force;
    while (std::cin >> Focus[0] >> Focus[1] >> Edge >> Extent[0] >> Extent[1]
           >> Origin[0] >> Origin[1] >> Force >> Cell[0] >> Cell[1] >> Cell[2]
           >> WrapExtent[0] >> WrapExtent[1] >> WrapExtent[2])
    {
        try
        {
            const auto Coordinate = WorldCell(Focus, Edge);
            const auto Window = RecentreWindow(Focus, Edge, Extent, Origin, Force != 0);
            const auto Wrapped = WrapCell(Cell, WrapExtent);
            std::cout << Coordinate[0] << ' ' << Coordinate[1] << ' '
                      << Window[0] << ' ' << Window[1] << ' '
                      << Wrapped[0] << ' ' << Wrapped[1] << ' ' << Wrapped[2] << '\n';
        }
        catch (const std::exception&)
        {
            std::cout << "invalid\n";
        }
    }
}
