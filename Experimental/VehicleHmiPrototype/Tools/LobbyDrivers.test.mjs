import { test } from "node:test";
import assert from "node:assert/strict";
import { LobbyDrivers, EmitLobbyPlateFacets } from "../Source/LobbyDrivers.js";
import { CourseMaterial } from "../Source/MaterialSpecification.js";
import { ControlVehicleSockets } from "../Source/VehicleSpecification.js";

test("every lobby car has a driver with a nametag and a rank", () => {
    assert.equal(LobbyDrivers.length, 5);   // hero + four display cars
    for (const Driver of LobbyDrivers) {
        assert.ok(Driver.NameTag.length >= 3 && Driver.NameTag.length <= 8);
        assert.match(Driver.Rank, /^(BRONZE|SILVER|GOLD|DIAMOND) (I|II|III|IV)$/);
    }
    assert.equal(new Set(LobbyDrivers.map((Driver) => Driver.NameTag)).size, 5);
});

test("plates sit at both IdPlate sockets and carry face, ink and the driver's tier badge", () => {
    const Tiers = { BRONZE: CourseMaterial.RankBronze, SILVER: CourseMaterial.RankSilver,
                    GOLD: CourseMaterial.RankGold, DIAMOND: CourseMaterial.RankDiamond };
    for (const Driver of LobbyDrivers) {
        const Facets = EmitLobbyPlateFacets(Driver);
        const Families = new Set(Facets.map((Facet) => Facet.Family));
        assert.ok(Families.has(CourseMaterial.PlateFace), Driver.NameTag);
        assert.ok(Families.has(CourseMaterial.PlateInk), Driver.NameTag);
        assert.ok(Families.has(Tiers[Driver.Rank.split(" ")[0]]), Driver.NameTag);
        // Both ends of the car: some geometry ahead of the front socket, some behind the rear one.
        const Xs = Facets.flatMap((Facet) => Facet.Corners.map((Corner) => Corner[0]));
        assert.ok(Math.max(...Xs) > ControlVehicleSockets.IdPlatePrimary[0], Driver.NameTag);
        assert.ok(Math.min(...Xs) < ControlVehicleSockets.IdPlateSecondary[0], Driver.NameTag);
        // And nothing floats far from the bumpers vertically.
        for (const Facet of Facets)
            for (const Corner of Facet.Corners)
                assert.ok(Math.abs(Corner[2]) < 0.30, Driver.NameTag);
    }
});

test("different ranks wear different badge metals", () => {
    const Badges = new Set(LobbyDrivers.map((Driver) =>
        EmitLobbyPlateFacets(Driver).find((Facet) =>
            [CourseMaterial.RankBronze, CourseMaterial.RankSilver, CourseMaterial.RankGold, CourseMaterial.RankDiamond]
                .includes(Facet.Family)).Family));
    assert.ok(Badges.size >= 3);
});
