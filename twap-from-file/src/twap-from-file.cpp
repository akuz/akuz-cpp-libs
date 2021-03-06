// Using Google C++ coding style
// Author: Andrey Kuzmenko
// Date: Apr 9, 2014

// IMPLEMENTATION NOTES:
//
// 1) Using int type for both, "time" and "order_id" because the task states that time
//    is measured in milliseconds since start of trading. I assume this code would be
//    used to calculate TWAP within one trading day. Therefore, int type should have
//    enough capacity to count all milliseconds in one trading day. Alternatively,
//    the type of "time" can be changed to long for handling longer periods.
//
// 2) Using all names from std namespace directly without prefix, which
//    might not always be the best idea, but it's ok for this test.
//
// 3) Utility classes implemented below are OrderBook and TWAP.
//    Please see documentation for each class.
//    Method main() is implemented last.

#include <map>
#include <cmath>
#include <limits>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
using namespace std;

// Contains current orders and automatically maintains max price.
//
// Order->price map contains prices arranged by order id, so that we
// can find the price of an order when it needs to be erased by id.
//
// Price->count map contains the number of orders for each price point.
// The map is sorted by price, so we can always obtain max price in O(1).
// When there are no more orders for some price point, it is removed.
//
// Important: Using double as a key is generally not a good idea,
// but it is justified in this case for price->count map because:
//
// 1) We are reading the prices from file and do *not* manipulate them
//    before using as keys. Therefore, for example, if 10.3 price is read,
//    it will be exactly equal (==) to the double 10.3 read from another line.
//
// 2) We are managing an order book, and therefore in realistic conditions
//    we actually expect to have many orders outstanding at the *same* prices.
//    Therefore, using order counting will greatly benefit the performance,
//    as opposed to storing *all* orders in a map by price.
//
// 3) Market prices are not infinitely divisible, but instead change by
//    ticks. Therefore, we can expect to have a *limited* number of price
//    points around the current mid price. This counting algorithm
//    will, again, be very effective in such conditions.
//
class OrderBook {

private:

	// keeps track of current orders & prices
	map<int, double> *order_price_map_;

	// counts number of orders at each price
	map<double, int> *price_count_map_;

public:

	OrderBook() {
		order_price_map_ = new map<int, double>();
		price_count_map_ = new map<double, int>();
	}

	~OrderBook() {
		delete order_price_map_;
		delete price_count_map_;
	}

	void insert_order(const int order_id, const double price) {

		const pair<map<int, double>::iterator, bool> order_pair
			= order_price_map_->insert(pair<int, double>(order_id, price));

		if (order_pair.second == false) {
			return; // order with this id already exists, not generating error, as per assumptions
		}

		const pair<map<double, int>::iterator, bool> price_pair
			= price_count_map_->insert(pair<double, int>(price, 1));

		if (price_pair.second == false) {
			price_pair.first->second++; // increment number of orders at this price
		}
	}

	void erase_order(const int order_id) {

		const map<int, double>::iterator order_it = order_price_map_->find(order_id);

		if (order_it == order_price_map_->end()) {
			return; // no order with this id exists, not generating error, as per assumptions
		}

		const double price = order_it->second;

		order_price_map_->erase(order_it);

		const map<double, int>::iterator price_it = price_count_map_->find(price);

		price_it->second--; // decrement order count at this price

		if (price_it->second <= 0) {
			price_count_map_->erase(price_it);
		}
	}

	double max_price() const {
		double result;
		if (price_count_map_->empty()) {
			result = numeric_limits<double>::quiet_NaN();
		} else {
			result = price_count_map_->rbegin()->first;
		}
		return result;
	}
};

// Calculates time-weighted average price (TWAP).
//
// Each time a new price is added, we can add the previous
// price to the average since it now lasted for the period
// since the last price until the new price.
//
// The new price will only affect the time-weighted
// average after some time has passed, when the next
// price point is added (valid price or NAN).
//
// If the new price is NAN, we just save it, and later it
// won't be taken into account for average calculation,
// because there was "no price" during this period.
//
class TWAP {

private:

	double last_price_;
	int last_time_;
	double avg_price_;
	int total_time_;

public:

	TWAP() {
		last_price_ = numeric_limits<double>::quiet_NaN();
		last_time_ = 0;
		avg_price_ = numeric_limits<double>::quiet_NaN();
		total_time_ = 0;
	}

	void next_price(const int time, const double price) {

		if (!isnan(last_price_)) {

			const int add_time = time - last_time_;
			if (add_time < 0)  {
				return; // time is not increasing,
						// but not generating error
						// as per assumptions
			}
			if (total_time_ > 0) {
				// could have counted the total weighted price
				// and then used total_weighted_price / total_time
				// however, the task states the precision is not an issue
				// so keeping like this for the sake of not overflowing
				const double new_total_time = total_time_ + add_time;
				avg_price_ = avg_price_  / new_total_time * total_time_
						   + last_price_ / new_total_time * add_time;
				total_time_ = new_total_time;
			} else {
				avg_price_ = last_price_;
				total_time_ = add_time;
			}
		}

		last_price_ = price;
		last_time_ = time;
	}

	double avg_price() {
		return avg_price_;
	}
};

// Program entry point.
//
// Note: the program doesn't output TWAP when the first order is processed
//       because TWAP is still undefined at this time (no time has passed).
//
int main(int argc, char *argv[]) {

	if (argc < 2) {
		cerr << "ERROR: Please specify file name as argument.";
		return 1;
	}

	const string file_name = argv[1];
	ifstream input_stream(file_name);

	if (!input_stream.good()) {
		cerr << "ERROR: Can't access input file: " << file_name;
		return 1;
	}

	OrderBook order_book;
	TWAP twap;

	for (string line; getline(input_stream, line); ) {

		istringstream line_stream(line);

		int time;
		if (!(line_stream >> time)) {
			continue; // no time in this line
		}

		string operation;
		if (!(line_stream >> operation)) {
			continue; // no operation in this line
		}

		int order_id;
		if (!(line_stream >> order_id)) {
			continue; // no order_id in this line
		}

		if (operation.compare("I") == 0) {

			double price;
			if (!(line_stream >> price)) {
				continue; // no price in this line
			}

			order_book.insert_order(order_id, price);

		} else if (operation.compare("E") == 0) {

			order_book.erase_order(order_id);

		} // else unknown operation, assuming this doesn't happen

		twap.next_price(time, order_book.max_price());

		const double twap_price = twap.avg_price();
		if (!isnan(twap_price)) {
			cout << twap_price << endl;
		}
	}

	return 0;
}
