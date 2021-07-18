#ifndef MCAD_REGIONMARKER_H
#define MCAD_REGIONMARKER_H
namespace llvm {
namespace mcad {
class RegionMarker {
  static constexpr unsigned BeginMark = 0b01;
  static constexpr unsigned EndMark = 0b10;

  unsigned Storage : 2;

  explicit RegionMarker(unsigned Storage) : Storage(Storage) {}

public:
  RegionMarker() : Storage(0) {}

  static RegionMarker getBegin() {
    return RegionMarker(BeginMark);
  }

  static RegionMarker getEnd() {
    return RegionMarker(EndMark);
  }

  inline bool isBegin() const { return Storage & BeginMark; }

  inline bool isEnd() const { return Storage & EndMark; }

  RegionMarker operator|(const RegionMarker &RHS) const {
    return RegionMarker(Storage | RHS.Storage);
  }

  RegionMarker &operator|=(const RegionMarker &RHS) {
    Storage |= RHS.Storage;
    return *this;
  }
};
} // end namespace mcad
} // end namespace llvm
#endif
