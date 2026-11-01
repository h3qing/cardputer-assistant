#include "theme_art.h"

#include "gen/icons_gen.h"
#include "gen/mando46_gen.h"
#include "gen/mando64_gen.h"

namespace cpa {
namespace theme {

ArtView art_of(ArtId id) {
  const uint8_t k = static_cast<uint8_t>(id);
  if (id == ArtId::Head) return ArtView{gen::kHead, gen::kHeadW, gen::kHeadH, false};
  if (id >= ArtId::Mando46 && id < ArtId::Mando64) {
    return ArtView{gen::kMando46[k - static_cast<uint8_t>(ArtId::Mando46)], gen::kMando46W, gen::kMando46H, true};
  }
  if (id >= ArtId::Mando64 && id < ArtId::Count) {
    return ArtView{gen::kMando64[k - static_cast<uint8_t>(ArtId::Mando64)], gen::kMando64W, gen::kMando64H, true};
  }
  return ArtView{};
}

}  // namespace theme
}  // namespace cpa
