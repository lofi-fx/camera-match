#include "State.h"
#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxGPURender.h>
#include <ofxParam.h>
#include <ofxProperty.h>
#include <cassert>
#include <cstring>
#include <dlfcn.h>
#include <map>
#include <string>
#include <vector>

struct Properties {
  std::map<std::pair<std::string, int>, std::string> strings;
  std::map<std::pair<std::string, int>, double> numbers;
};
static Properties effect, source, output;
static std::map<std::string, Properties> parameters;
static OfxPropertySuiteV1 properties{};
static OfxImageEffectSuiteV1 effects{};
static OfxParameterSuiteV1 params{};
static Properties &props(OfxPropertySetHandle h) { return *reinterpret_cast<Properties *>(h); }
static OfxStatus setString(OfxPropertySetHandle h, const char *key, int i, const char *value) {
  props(h).strings[{key, i}] = value; return kOfxStatOK;
}
static OfxStatus setDouble(OfxPropertySetHandle h, const char *key, int i, double value) {
  props(h).numbers[{key, i}] = value; return kOfxStatOK;
}
static OfxStatus setInt(OfxPropertySetHandle h, const char *key, int i, int value) {
  return setDouble(h, key, i, value);
}
static OfxStatus getProperties(OfxImageEffectHandle, OfxPropertySetHandle *out) {
  *out = reinterpret_cast<OfxPropertySetHandle>(&effect); return kOfxStatOK;
}
static OfxStatus getParameters(OfxImageEffectHandle, OfxParamSetHandle *out) {
  *out = reinterpret_cast<OfxParamSetHandle>(&parameters); return kOfxStatOK;
}
static OfxStatus clipDefine(OfxImageEffectHandle, const char *name, OfxPropertySetHandle *out) {
  *out = reinterpret_cast<OfxPropertySetHandle>(std::strcmp(name, "Source") == 0 ? &source : &output);
  return kOfxStatOK;
}
static OfxStatus paramDefine(OfxParamSetHandle, const char *type, const char *name, OfxPropertySetHandle *out) {
  assert(parameters.count(name) == 0);
  auto &p = parameters[name];
  p.strings[{kOfxParamPropType, 0}] = type;
  *out = reinterpret_cast<OfxPropertySetHandle>(&p); return kOfxStatOK;
}
static const void *fetchSuite(OfxPropertySetHandle, const char *name, int version) {
  if (version != 1) return nullptr;
  if (std::strcmp(name, kOfxPropertySuite) == 0) return &properties;
  if (std::strcmp(name, kOfxImageEffectSuite) == 0) return &effects;
  if (std::strcmp(name, kOfxParameterSuite) == 0) return &params;
  return nullptr;
}
int main(int argc, char **argv) {
  assert(argc == 2);
  void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  assert(library);
  auto count = reinterpret_cast<int (*)()>(dlsym(library, "OfxGetNumberOfPlugins"));
  auto get = reinterpret_cast<OfxPlugin *(*)(int)>(dlsym(library, "OfxGetPlugin"));
  assert(count && get && count() == 1);
  properties.propSetString = setString;
  properties.propSetDouble = setDouble;
  properties.propSetInt = setInt;
  effects.getPropertySet = getProperties;
  effects.getParamSet = getParameters;
  effects.clipDefine = clipDefine;
  params.paramDefine = paramDefine;
  OfxHost host{nullptr, fetchSuite};
  auto plugin = get(0);
  plugin->setHost(&host);
  assert(plugin->mainEntry(kOfxActionLoad, nullptr, nullptr, nullptr) == kOfxStatOK);
  auto handle = reinterpret_cast<OfxImageEffectHandle>(&effect);
  assert(plugin->mainEntry(kOfxActionDescribe, handle, nullptr, nullptr) == kOfxStatOK);
  assert(effect.strings.at({kOfxImageEffectPropMetalRenderSupported, 0}) == "true");
  assert(plugin->mainEntry(kOfxImageEffectActionDescribeInContext, handle, nullptr, nullptr) == kOfxStatOK);
  for (const char *removed : {"matchMethod", "hue", "sat", "exposure", "neutral"})
    assert(parameters.count(removed) == 0);
  std::vector<std::string> adjustments;
  for (const auto &[name, p] : parameters) {
    auto parent = p.strings.find({kOfxParamPropParent, 0});
    if (parent != p.strings.end() && parent->second == "adjustmentsGroup") adjustments.push_back(name);
  }
  assert((adjustments == std::vector<std::string>{"biasWeight", "rbfExposure", "rbfSat"}));
  for (const char *name : {"biasWeight", "rbfSat", "rbfExposure"}) {
    auto &p = parameters.at(name);
    assert((p.numbers[{kOfxParamPropSecret, 0}] == 0));
    assert(p.numbers.at({kOfxParamPropDefault, 0}) == 100);
    assert(p.numbers.at({kOfxParamPropMin, 0}) == 0);
    assert(p.numbers.at({kOfxParamPropMax, 0}) == (std::strcmp(name, "biasWeight") == 0 ? 200 : 100));
  }
  assert(parameters.at("biasWeight").strings.at({kOfxPropLabel, 0}) == "Reference match %");
  for (const char *name : {"captureHero", "analyze", "listHeroes", "heroName", "heroToApply",
                          "chartModel", "rotation", "mirror", "showOverlay", "editMode", "refit",
                          "bypass", "patchSelector", "included", "sampleInset", "cameraMatchState"})
    assert(parameters.count(name) == 1);
  cm::Persistent empty;
  assert(cm::deserialize(parameters.at("cameraMatchState").strings.at({kOfxParamPropDefault, 0}), empty));
  assert(!empty.hasHero && !empty.hasTarget && !empty.solution.valid);
  assert(plugin->mainEntry(kOfxActionUnload, nullptr, nullptr, nullptr) == kOfxStatOK);
  dlclose(library);
}
