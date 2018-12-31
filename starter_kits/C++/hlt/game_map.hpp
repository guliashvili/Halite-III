#pragma once

#include "types.hpp"
#include "map_cell.hpp"
#include "constants.hpp"

#include <vector>
using namespace std;

namespace hlt {
    struct GameMap {
        int width;
        int height;
        std::vector<std::vector<MapCell>> cells;
        PlayerId me = -1;

        void init(PlayerId me_){
          me = me_;
        }

        MapCell* at(const Position& position) {
            return &cells[position.y][position.x];
        }

        inline MapCell* at(const Entity& entity) {
            return at(entity.position);
        }

        inline MapCell* at(const Entity* entity) {
            return at(entity->position);
        }

        inline MapCell* at(const std::shared_ptr<Entity>& entity) {
            return at(entity->position);
        }

        int calculate_distance(const Position& source, const Position& target) {
            const int dx = std::abs(source.x - target.x);
            const int dy = std::abs(source.y - target.y);

            const int toroidal_dx = std::min(dx, width - dx);
            const int toroidal_dy = std::min(dy, height - dy);

            return toroidal_dx + toroidal_dy;
        }

        std::vector<Direction> get_unsafe_moves(const Position& source, const Position& destination) {
            const int dx = std::abs(source.x - destination.x);
            const int dy = std::abs(source.y - destination.y);
            const int wrapped_dx = width - dx;
            const int wrapped_dy = height - dy;

            std::vector<Direction> possible_moves;

            if (source.x < destination.x) {
                possible_moves.push_back(dx > wrapped_dx ? Direction::WEST : Direction::EAST);
            } else if (source.x > destination.x) {
                possible_moves.push_back(dx < wrapped_dx ? Direction::WEST : Direction::EAST);
            }

            if (source.y < destination.y) {
                possible_moves.push_back(dy > wrapped_dy ? Direction::NORTH : Direction::SOUTH);
            } else if (source.y > destination.y) {
                possible_moves.push_back(dy < wrapped_dy ? Direction::NORTH : Direction::SOUTH);
            }

            return possible_moves;
        }

        bool has_my_structure(const Position& position){
          const auto& cell = at(position);
          return cell->has_structure() && cell->structure->owner == me;
        }

        std::vector<Direction> get_safe_moves(Position& source, const Position& destination, bool recall=false) {
          auto directions = get_unsafe_moves(source, destination);
          std::vector<Direction> safe_directions;

          for(auto direction : directions){
            const auto& next_pos = source.directional_offset(direction);
            if(!at(next_pos)->is_occupied() || (recall &&  has_my_structure(next_pos))){
              safe_directions.push_back(direction);
            }
          }
          return safe_directions;
        }

        void navigate(std::shared_ptr<Ship> ship, const Position& destination) {
            // get_unsafe_moves normalizes for us
            at(destination)->mark_unsafe(ship);

            #ifdef DEBUG
            //TODO
            #endif
        }
        void navigate(std::shared_ptr<Ship> ship, const Direction& direction) {
            // get_unsafe_moves normalizes for us
            navigate(ship, ship->position.directional_offset(direction));
        }

        bool can_move(std::shared_ptr<Ship> ship){
            return at(ship->position)->halite / constants::MOVE_COST_RATIO <= ship->halite;
        }

        void _update();
        static std::unique_ptr<GameMap> _generate();
    };
}
