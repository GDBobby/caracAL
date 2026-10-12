#pragma once

#include <string_view>

namespace caracal {

    // Adventure Land clone() overlay for a JS game VM. Kept as source text because
    // the official client still evaluates JavaScript; this is data, not a host language.
    inline constexpr std::string_view realm_safe_clone_overlay = R"JS(
  (function () {
    function caracAL_tag(value) {
      return Object.prototype.toString.call(value);
    }

    function caracAL_is_date(value) {
      return caracAL_tag(value) === "[object Date]";
    }

    function caracAL_is_plain_object(value) {
      if (value === null || typeof value !== "object") return false;
      if (Array.isArray(value) || caracAL_is_date(value)) return false;
      var tag = caracAL_tag(value);
      if (tag === "[object Object]") return true;
      try {
        return Object.getPrototypeOf(value) === null;
      } catch (e) {
        return false;
      }
    }

    function clone(obj, args) {
      if (!args) args = {};
      if (!args.seen && args.seen !== []) args.seen = [];
      if (null == obj) return obj;
      if (args.simple_functions && typeof is_function === "function" && is_function(obj)) {
        return "[clone]:" + obj.toString().substring(0, 40);
      }
      if ("object" != typeof obj) return obj;
      if (caracAL_is_date(obj)) {
        var date_copy = new Date();
        date_copy.setTime(obj.getTime());
        return date_copy;
      }
      if (Array.isArray(obj)) {
        args.seen.push(obj);
        var array_copy = [];
        for (var i = 0; i < obj.length; i++) {
          array_copy[i] = clone(obj[i], args);
        }
        return array_copy;
      }
      if (caracAL_is_plain_object(obj) || (typeof obj === "object" && obj instanceof Object)) {
        args.seen.push(obj);
        var object_copy = {};
        for (var attr in obj) {
          if (Object.prototype.hasOwnProperty.call(obj, attr)) {
            if (args.seen.indexOf(obj[attr]) !== -1) {
              object_copy[attr] = "circular_attribute[clone]";
              continue;
            }
            object_copy[attr] = clone(obj[attr], args);
          }
        }
        return object_copy;
      }
      throw "type not supported";
    }

    clone.__caracAL_realm_safe = true;
    globalThis.clone = clone;
  })();
  )JS";

} // namespace caracal
