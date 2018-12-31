#include "hlt/game.hpp"
#include "hlt/constants.hpp"
#include "hlt/log.hpp"
#include "hlt/genes.hpp"

#include <random>
#include <ctime>
#include <unordered_map>
#include <optional>
#include <tuple>
#include <array>
#include <algorithm>
#include <ctime>
#include <ratio>
#include <chrono>

using namespace std;
using namespace hlt;

unique_ptr<Genes> genes;
Game game;
shared_ptr<Player> me;

void navigate(const shared_ptr<Ship> ship, const Direction& direction,vector<tuple<shared_ptr<Ship>, Direction>>& direction_queue){
  game.game_map->navigate(ship, direction);
  direction_queue.emplace_back(ship, direction);
}

Direction greedySquareMove(shared_ptr<Ship> ship, Position& target, bool recall=false){
  auto directions = game.game_map->get_safe_moves(ship->position, target, recall);
  if(directions.size() == 0){
    return Direction::STILL;
  }else if(directions.size() == 1){
    return directions[0];
  }else {
    auto a = game.game_map->at(ship->position.directional_offset(directions[0]));
    auto b = game.game_map->at(ship->position.directional_offset(directions[1]));
    if(a->halite > b->halite){
      swap(a,b);
    }
    if(a->move_cost() > ship->halite - game.game_map->at(ship->position)->move_cost()){
      return directions[1];
    }else if(b->move_cost() - a->move_cost() < genes->greedy_walk_randomisation_margin){
      srand(ship->id * game.turn_number * target.x * target.y * genes->seed);
      return directions[rand()%directions.size()];
    }else{
      return directions[0];
    }
  }

}

pair<int, shared_ptr<Entity>> getMinDistanceToDropoff(Position& position, const vector<shared_ptr<Entity>>& dropoffs){
  pair<int, shared_ptr<Entity>>  minDistance;
  minDistance.first = 999999999;
  for(const auto& dropoff : dropoffs){
    int cur_distance = game.game_map->calculate_distance(position, dropoff->position);
    if(cur_distance < minDistance.first){
      minDistance.first = cur_distance;
      minDistance.second = dropoff;
    }
  }
  return minDistance;
}

std::optional<Direction> isRecallTime(shared_ptr<Ship> ship){
  auto minDstDropoff = getMinDistanceToDropoff(ship->position, me->all_dropoffs);
  if(constants::MAX_TURNS - game.turn_number - genes->extra_time_for_recall > (minDstDropoff.first+me->ships.size()/10.0/me->all_dropoffs.size()) ){
      return {};
  }
  return greedySquareMove(ship, minDstDropoff.second->position, true);// TODO might stand still when blocked
}


std::optional<Direction> shouldGoHome(shared_ptr<Ship> ship){
  if(ship->halite < constants::MAX_HALITE*9/10){
    return {};
  }

  auto minDstDropoff = getMinDistanceToDropoff(ship->position, me->all_dropoffs);
  return greedySquareMove(ship, minDstDropoff.second->position); // TODO might stand still when blocked
}


vector<tuple<unsigned,Position, double> > _candidates;
int ship_pair_st[64][64] = {{0}};
int PAIR_MARK = 1;

void pair_ships(vector<shared_ptr<Ship> >& ships, vector<tuple<shared_ptr<Ship>, Direction>>&  ship_directions){
  PAIR_MARK++;
  auto& candidates = _candidates;
  candidates.resize(0);

  vector<tuple<shared_ptr<Ship>, Direction>> ret;

  {
    Position pos;
    for(int& y = pos.y = 0; y < constants::HEIGHT; y++){
        for(int& x = pos.x = 0; x < constants::WIDTH; x++){
            if(ship_pair_st[y][x] == PAIR_MARK){
              continue;
            }

            auto target_cell = game.game_map->at(pos);
            auto target_halite_amount = target_cell->halite;
            if(target_cell->extract_halite() == 0){
              continue;
            }
            //TODO SOMEONE ELSE SITS ON THAT

            for(unsigned i = 0; i < ships.size(); i++){
              auto ship = ships[i];
              int distance = game.game_map->calculate_distance(ship->position, pos);
              candidates.emplace_back(i, pos,
                min(constants::MAX_HALITE - ship->halite, target_halite_amount - 3) / double(distance+1));
            }
        }
    }
  }

  sort(candidates.begin(), candidates.end(), [ ]( const auto& lhs, const auto& rhs )
    {
       return std::get<2>(lhs) > std::get<2>(rhs);
    });

  for(unsigned I = 0; I < candidates.size(); I++){
    auto i = get<0>(candidates[I]);
    auto& pos = get<1>(candidates[I]);
    if(ships[i] == nullptr){
      continue;
    }
    if(ship_pair_st[pos.y][pos.x] == PAIR_MARK){
      continue;
    }
    auto& ship = ships[i];
    if(pos != ship->position && game.game_map->get_safe_moves(ship->position, pos).size() == 0){
      continue;
    }

    navigate(ship, greedySquareMove(ship, pos), ship_directions);

    ship_pair_st[pos.y][pos.x] = PAIR_MARK;
    ship = nullptr;
  }

  for(auto& ship : ships){
    if(ship != nullptr){
      navigate(ship, Direction::STILL, ship_directions);
      //log::log("pairstill " + to_string(ship->id));
    }
  }
}


// bool GOING_HOME[1000] = {false};
int NUM_OF_MOVES_FROM_HOME[1000] = {0};
int AVERAGE_TIME_TO_HOME = 0;

bool should_ship_new_ship(){
  int total_me = game.me->ships.size();
  if(total_me == 0){
    return true;
  }

  int total_halite = 0;
  {
    Position position;
    for(int& i = position.y = 0; i < constants::HEIGHT; i++){
      for(int& j = position.x = 0; j < constants::WIDTH; j++){
        total_halite += max(0, game.game_map->at(position)->halite - genes->total_halite_margin_substr);
      }
    }
  }

  int total_ships_count = 0;
  for(const auto& player : game.players){
    total_ships_count += player->ships.size();
  }

  int current_halite_prediction = total_halite * total_me / total_ships_count;
  int next_halite_prediction = total_halite * (total_me + 1) / (total_ships_count + 1);

  return next_halite_prediction - current_halite_prediction > genes->margin_to_create_new_ship
    && constants::MAX_TURNS - AVERAGE_TIME_TO_HOME > genes->ship_spawn_step_margin;
}

vector<shared_ptr<Ship>> oplock_doStepNoStill(vector<shared_ptr<Ship> > ships, vector<tuple<shared_ptr<Ship>, Direction>>& direction_queue_original){
    vector<tuple<shared_ptr<Ship>, Direction>> direction_queue_temporary;
    direction_queue_temporary.reserve(ships.size());

    vector<shared_ptr<Ship>> ready_to_pair;
    vector<shared_ptr<Ship> > going_home;
    sort(ships.begin(), ships.end(), [ ]( const auto& lhs, const auto& rhs )
      {
         return  get<0>(getMinDistanceToDropoff(lhs->position, game.me->all_dropoffs))
            < get<0>(getMinDistanceToDropoff(rhs->position, game.me->all_dropoffs));
      });
    for (const auto& ship : ships) {
        // if(!game_map->can_move(ship)){ Should be checked already
        //   command_queue.push_back(ship->stay_still());
        // }else
        if(auto recall=isRecallTime(ship)){
          //log::log("recalltime");
          navigate(ship, recall.value(), direction_queue_temporary);
        }else if(auto goHome=shouldGoHome(ship)){
          going_home.push_back(ship);
        }else{
          ready_to_pair.push_back(ship);
        }
    }

    pair_ships(ready_to_pair, direction_queue_temporary);

    for(auto& ship : going_home){
      if(auto goHome=shouldGoHome(ship)){
        navigate(ship, goHome.value(), direction_queue_temporary);
        // if(goHome.value() == Direction::STILL){
        //   //log::log("gohomestill " + to_string(ship->id));
        // }
      }else{
        navigate(ship,  Direction::STILL, direction_queue_temporary);
        // if(goHome.value() == Direction::STILL){
        //   //log::log("nogohomestill " + to_string(ship->id));
        // }
      }
    }

    bool was_blocker = false;
    for(auto& item : direction_queue_temporary){
      if(get<1>(item) == Direction::STILL){
          was_blocker = true;
          break;
      }
    }

    if(was_blocker){
      vector<shared_ptr<Ship>> ships_no_still;
      ships_no_still.reserve(ships.size());
      for(auto& [ship, direction] : direction_queue_temporary){
        if(direction != Direction::STILL){
          auto target_pos = ship->position.directional_offset(direction);
          if(game.game_map->at(target_pos)->ship == ship){
            game.game_map->at(target_pos)->mark_safe();
          }
          ships_no_still.push_back(ship);
        }else{
          game.game_map->at(ship->position)->mark_unsafe(ship);
          direction_queue_original.emplace_back(ship, direction);
          //log::log("stay stil; " + to_string(ship->id));
        }
      }

      return ships_no_still;
    }else{
      // for(auto& [ship, direction] : direction_queue_temporary){
      //   //log::log("the end: " + to_string(ship->id));
      // }
      direction_queue_original.insert(direction_queue_original.end(), direction_queue_temporary.begin(), direction_queue_temporary.end());
      return {};
    }
}

bool doStep(vector<tuple<shared_ptr<Ship>, Direction>>& direction_queue){
  using namespace std::chrono;

  // high_resolution_clock::time_point t1 = high_resolution_clock::now();

  me = game.me;
  unique_ptr<GameMap>& game_map = game.game_map;
  game_map->init(me->id);

  int SAVINGS = 0;

  vector<shared_ptr<Ship> > ships;

  for (const auto& ship : me->ships) {
      if(game_map->has_my_structure(ship->position)){
        // GOING_HOME[ship->id] = false;
        AVERAGE_TIME_TO_HOME = AVERAGE_TIME_TO_HOME * genes->average_time_home_decay + NUM_OF_MOVES_FROM_HOME[ship->id] * (1-genes->average_time_home_decay);
        NUM_OF_MOVES_FROM_HOME[ship->id] = 0;
      }
      if(!game_map->can_move(ship)){
        //log::log("can not move " + to_string(ship->id));
        navigate(ship, Direction::STILL, direction_queue);
      }else{
        game.game_map->at(ship->position)->mark_safe();
        ships.push_back(ship);
      }
  }

  // int num_of_turns = 0;
  while((ships=oplock_doStepNoStill(ships, direction_queue)).size() != 0){
    // num_of_turns++;
    //log::log("\n\n");
  }


  for (const auto& ship : me->ships) {
      NUM_OF_MOVES_FROM_HOME[ship->id]++;
  }

  //log::log("doStep duration: " + to_string(duration_cast<duration<double>>(high_resolution_clock::now() - t1).count()) + " num: " + to_string(num_of_turns)  + " ships: " + to_string(me->ships.size()));
  auto ret = me->halite - SAVINGS >= constants::SHIP_COST &&  !(game_map->at(me->shipyard->position)->is_occupied()) && should_ship_new_ship();
  return ret;
}

int main(int argc, const char* argv[]) {

    _candidates.reserve(constants::WIDTH * constants::HEIGHT * 100);
    // At this point "game" variable is populated with initial map data.
    // This is a good place to do computationally expensive start-up pre-processing.
    // As soon as you call "ready" function below, the 2 second per turn timer will start.
    game.ready("MyCppBot");
    genes = make_unique<Genes>(argc, argv);
    srand(genes->seed);
    //log::log("Successfully created bot! My Player ID is " + to_string(game.my_id) + ". Bot rng seed is " + to_string(genes->seed) + ".");

    vector<Command> command_queue;
    vector<tuple<shared_ptr<Ship>, Direction>> direction_queue;
    while(1){
      game.update_frame();

      command_queue.resize(0);
      command_queue.reserve(game.me->ships.size() + 1);
      direction_queue.resize(0);
      direction_queue.reserve(game.me->ships.size());


      if(doStep(direction_queue)){
        command_queue.push_back(command::spawn_ship());
      }

      for(auto& [ship, direction] : direction_queue){
        command_queue.push_back(command::move(ship->id, direction));
      }

      if (!game.end_turn(command_queue)) {
          break;
      }
    }

    return 0;
}
