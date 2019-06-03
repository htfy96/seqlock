#include <iostream>
#include <thread>
#include "seqlock.hpp"
using namespace std;

struct TestData {
  char c;
  int w;
  double x;
  int arr[2];  // NOLINT(modernize-avoid-c-arrays)
  friend bool operator==(const TestData& d1, const TestData& d2) {
    return d1.c == d2.c && d1.w == d2.w && d1.x == d2.x &&
           d1.arr[0] == d2.arr[0] && d1.arr[1] == d2.arr[1];
  }
  friend bool operator!=(const TestData& d1, const TestData& d2) {
    return !(d1 == d2);
  }
};
const TestData to_write_1{'3', 0x2561, -120.000801, 52651, -12151},
    to_write_2{'2', 3, 4.57, 12, 1};

void print_every(int i, int every_cnt, const char* msg) {
  if (i % every_cnt == 0)
    cout << msg << " " << i << endl;
}

int main() {
  seqlock::SeqLock<TestData> lk{to_write_2};
  thread w1([&]() {
    for (int i = 0; i < 100000000; ++i) {
      lk.write(to_write_1);
      print_every(i, 10000000, "w1");
    }
  });
  thread w2([&]() {
    for (int i = 0; i < 90000000; ++i) {
      auto writer = lk.get_writer();
      double prev_x = writer.read_member(&TestData::x);
      if (prev_x != to_write_1.x && prev_x != to_write_2.x)
        abort();
      writer.write_member(&TestData::c, to_write_2.c);
      writer.write_member(&TestData::w, to_write_2.w);
      writer.write_member(&TestData::x, to_write_2.x);
      writer.write_member(&TestData::arr, to_write_2.arr);
      double new_x = writer.read_member(&TestData::x);
      if (new_x != to_write_2.x)
        abort();
      print_every(i, 10000000, "w2");
    }
  });
  thread r1([&]() {
    for (int i = 0; i < 200000000; ++i) {
      TestData r = lk.load();
      if (r != to_write_1 && r != to_write_2)
        abort();
      print_every(i, 10000000, "r2");
    }
  });
  thread r2([&]() {
    for (int i = 0; i < 170000000; ++i) {
      auto r = lk.load_members(&TestData::x, &TestData::w);
      auto to_write_1_tp = std::make_tuple(to_write_1.x, to_write_1.w);
      auto to_write_2_tp = std::make_tuple(to_write_2.x, to_write_2.w);
      if (r != to_write_1_tp && r != to_write_2_tp)
        abort();
      print_every(i, 10000000, "r2");
    }
  });
  w1.join();
  w2.join();
  r1.join();
  r2.join();
}
