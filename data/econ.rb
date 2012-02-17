Space::System.delete_all
Econ::Location.delete_all
Econ::Commodity.delete_all

midgard_system = Space::System.create!(name: "Midgard")
vespus_system = Space::System.create!(name: "Vespus")
pembroke_system = Space::System.create!(name: "Pembroke")
cabrea_system = Space::System.create!(name: "Cabrea")

sting = midgard_system.locations.create!(name: "Sting")
vespus_i = vespus_system.locations.create!(name: "Vespus I")
cabrea_ii = cabrea_system.locations.create!(name: "Cabrea II")
inferno = cabrea_system.locations.create!(name: "Inferno")

live_animals = Econ::Commodity.create!(name: "live animals", master_price: 20, price_volatility: 2)
live_animals.demand_factors.create!(location: sting, factor: -2)
live_animals.demand_factors.create!(location: vespus_i, factor: 1)
live_animals.demand_factors.create!(location: cabrea_ii, factor: 1)
live_animals.demand_factors.create!(location: inferno, factor: 0)


