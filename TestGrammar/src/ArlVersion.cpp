///////////////////////////////////////////////////////////////////////////////
/// @file
/// @brief ArlVersion class definition
///
/// @copyright
/// Copyright 2022 PDF Association, Inc. https://www.pdfa.org
/// SPDX-License-Identifier: Apache-2.0
///
/// @remark
/// This material is based upon work supported by the Defense Advanced
/// Research Projects Agency (DARPA) under Contract No. HR001119C0079.
/// Any opinions, findings and conclusions or recommendations expressed
/// in this material are those of the author(s) and do not necessarily
/// reflect the views of the Defense Advanced Research Projects Agency
/// (DARPA). Approved for public release.
///
/// @author Peter Wyatt, PDF Association
///
///////////////////////////////////////////////////////////////////////////////

#include "ArlVersion.h"
#include "ArlingtonTSVGrammarFile.h"
#include "ArlPredicates.h"
#include "LRParsePredicate.h"
#include "utils.h"
#include "PDFFile.h"

#include <memory>
#include <algorithm>
#include <cassert>
#include <regex>

using namespace ArlingtonPDFShim;

/// @brief Constructor to handle version complexities
///
/// @param[in] obj          PDF object
/// @param[in] vec          the row from the Arlington TSV file (including all predicates and complexity ([];[];[]))
/// @param[in] pdf_ver      PDF version multiplied by 10
ArlVersion::ArlVersion(ArlPDFObject* obj, std::vector<std::string> vec, const int pdf_ver)
    : arl_version(0), version_reason(ArlVersionReason::Unknown), arl_type_index(-1)
{
    tsv = vec;

    // Determine the Arlington equivalent for the PDF Object
    assert(obj != nullptr);
    switch (obj->get_object_type())
    {
    case PDFObjectType::ArlPDFObjTypeNumber:
    {
        ArlPDFNumber* numobj = (ArlPDFNumber*)obj;
        if (numobj->is_integer_value())
            arl_type_of_pdf_object = "integer";         // or "bitmask"
        else
            arl_type_of_pdf_object = "number";
    }
    break;
    case PDFObjectType::ArlPDFObjTypeBoolean:     arl_type_of_pdf_object = "boolean"; break;
    case PDFObjectType::ArlPDFObjTypeName:        arl_type_of_pdf_object = "name"; break;
    case PDFObjectType::ArlPDFObjTypeNull:        arl_type_of_pdf_object = "null"; break;
    case PDFObjectType::ArlPDFObjTypeStream:      arl_type_of_pdf_object = "stream"; break;
    case PDFObjectType::ArlPDFObjTypeString:      arl_type_of_pdf_object = "string"; break;     // or "date" or "string-*"...
    case PDFObjectType::ArlPDFObjTypeArray:       arl_type_of_pdf_object = "array"; break;      // or "rectangle" or "matrix"
    case PDFObjectType::ArlPDFObjTypeDictionary:  arl_type_of_pdf_object = "dictionary"; break; // or "name-tree" or "number-tree"
    case PDFObjectType::ArlPDFObjTypeReference:
        assert(false && "ArlPDFObjTypeReference for ArlVersion()");
        arl_type_of_pdf_object = "null";
        break;
    default:
        assert(false && "unexpected type for ArlVersion()");
        arl_type_of_pdf_object = "null";
        break;
    }
    assert(FindInVector(v_ArlAllTypes, arl_type_of_pdf_object));

    // Set the PDF version being tested
    assert((pdf_ver >= 10) && ((pdf_ver <= 17) || (pdf_ver == 20)));
    pdf_version = pdf_ver;

    // Determine the type we will match from Arlington TSV 'Type' field data.
    // The Type field is complex ([];[];[]) and can have version predicates!
    // - try exact match first
    // - if object was integer look for bitmask
    // - if object was array look for rectangle and matrix
    // - if object was dictionary look for name-tree or number-tree
    // - if object was string look for date or string-*
    std::string arl_types = vec[TSV_TYPE];
    bool found = false;
    std::vector<std::string>  arl_t = split(arl_types, ';');
    for (int i = 0; i < (int)arl_t.size(); i++) {
        std::string t = arl_t[i];
        if ((t.size() > 0) && (t[0] == '['))
            t = t.substr(1, t.size() - 2);  // strip enclosing "[...]"
        if ((t == "number") && (arl_type_of_pdf_object == "integer")) {
            // Can always use integer in place of a number
            arl_type = arl_type_of_pdf_object = "number";
            version_reason = ArlVersionReason::OK;
            arl_type_index = i;
            found = true;
            break;
        }
        else if (t.find(arl_type_of_pdf_object) != std::string::npos) {
            if (t == arl_type_of_pdf_object) {
                // Found an exact match without any version predicates
                arl_type = arl_type_of_pdf_object;
                version_reason = ArlVersionReason::OK;
                arl_type_index = i;
                found = true;
                break;
            }
            else  // Found an exact match but wrapped in version predicates so fallthrough
                break;
        }
    } // for

    if (!found) {
        std::smatch     m;
        for (int i = 0; i < (int)arl_t.size() && !found; i++) {
            std::string t = arl_t[i];
            if (t.find("fn:") != std::string::npos) {
                // Predicate - remove...
                if (std::regex_search(t, m, r_Types) && m.ready() && (m.size() == 4)) {
                    // m[1] = predicate function name (no "fn:" or '(')
                    // m[2] = PDF version "x.y"
                    // m[3] = Arlington pre-defined type
                    std::string s = m[1].str();
                    if (s == "SinceVersion") {
                        if (pdf_version >= arl_version)
                            version_reason = ArlVersionReason::OK;
                        else
                            version_reason = ArlVersionReason::Before_fnSinceVersion;
                    }
                    else if (s == "Deprecated") {
                        if (pdf_version >= arl_version)
                            version_reason = ArlVersionReason::Is_fnDeprecated;
                        else
                            version_reason = ArlVersionReason::OK;
                    }
                    else if (s == "IsPDFVersion") {
                        if (pdf_version == arl_version)
                            version_reason = ArlVersionReason::OK;
                        else
                            version_reason = ArlVersionReason::Not_fnIsPDFVersion;
                    }
                    else {
                        assert(s == "BeforeVersion");
                        if (pdf_version < arl_version)
                            version_reason = ArlVersionReason::OK;
                        else
                            version_reason = ArlVersionReason::After_fnBeforeVersion;
                    }

                    s = m[2].str();
                    assert(FindInVector(v_ArlPDFVersions, s));
                    arl_version = ((s[0] - '0') * 10) + (s[2] - '0');

                    assert(FindInVector(v_ArlAllTypes, m[3]));
                    t = m[3];
                }
                else {
                    assert(false && "unexpected predicate in Type field!");
                }
            }

            // 't' should now be cleaned of predicates
            if ((arl_type_of_pdf_object == t) ||
                ((arl_type_of_pdf_object == "integer") && (t == "bitmask")) ||
                ((arl_type_of_pdf_object == "array") && (t =="rectangle")) ||
                ((arl_type_of_pdf_object == "array") && (t == "matrix")) ||
                ((arl_type_of_pdf_object == "dictionary") && (t == "name-tree")) ||
                ((arl_type_of_pdf_object == "dictionary") && (t == "number-tree")) ||
                ((arl_type_of_pdf_object == "string") && (t == "date")) ||
                ((arl_type_of_pdf_object == "string") && (t.find("string-") != std::string::npos))) {
                arl_type_index = i;
                arl_type = t;
                found = true;
                if (version_reason == ArlVersionReason::Unknown)
                    version_reason = ArlVersionReason::OK;
            }
        } // for
    } // if !found

    // Override predicates with SinceVersion and DeprecatedIn fields
    int since_ver = ((vec[TSV_SINCEVERSION][0] - '0') * 10) + (vec[TSV_SINCEVERSION][2] - '0');
    assert((since_ver >= 10) && ((since_ver <= 17) || (since_ver == 20)));
    if (found && (pdf_version < since_ver)) {
        arl_version = since_ver;
        version_reason = ArlVersionReason::Before_fnSinceVersion;
    }

    if (found && (vec[TSV_DEPRECATEDIN] != "")) {
        int deprecated_ver = ((vec[TSV_DEPRECATEDIN][0] - '0') * 10) + (vec[TSV_DEPRECATEDIN][2] - '0');
        assert((deprecated_ver >= 10) && ((deprecated_ver <= 17) || (deprecated_ver == 20)));
        if (pdf_version >= deprecated_ver) {
            arl_version = deprecated_ver;
            version_reason = ArlVersionReason::Is_fnDeprecated;
        }
        if ((deprecated_ver == since_ver) && (pdf_version != deprecated_ver)) {
            arl_version = deprecated_ver;
            version_reason = ArlVersionReason::Not_fnIsPDFVersion;
        }
    }

    // Fix-up / sanity logic due to predicates vs SinceVersion / DeprecatedIn fields
    if (found && (version_reason == ArlVersionReason::Is_fnDeprecated) && (arl_version > pdf_version)) {
        found = true;
        version_reason = ArlVersionReason::OK;
    }

    // Regex may have matched and we prematurely set a reason, but still not found
    if (!found)
        version_reason = ArlVersionReason::Unknown;

    assert((found && (arl_type.size() > 0) && (arl_type_index >= 0)) || (!found && (arl_type.size() == 0) && (arl_type_index < 0)));
    assert((found && (version_reason != ArlVersionReason::Unknown)) || (!found && (version_reason == ArlVersionReason::Unknown)));
}


/// @brief Return an appropriate reduced Arlington Link set AFTER processing predicates for the current PDF object and PDF version.
/// Thus deprecated links will be processed away based on the PDF version and NOT returned.
///
/// @param[in] arl_links   a raw Arlington 'Links' field, including complex ([];[];[]) and predicates
///
/// @returns a reduced set (vector) of Arlington Links appropriate for the type of PDF object and PDF version.
/// Or empty vector if nothing appropriate.
std::vector<std::string>  ArlVersion::get_appropriate_linkset(std::string arl_links) {
    std::vector<std::string>      retval;

    if ((arl_type_index < 0) || (arl_links == ""))
        return retval; // empty vector of strings (i.e. no Links)

    std::vector<std::string>    links = split(arl_links, ';');
    assert(arl_type_index < (int)links.size());
    std::string                 appropriate_links = links[arl_type_index];
    assert(appropriate_links[0] == '[');
    appropriate_links = appropriate_links.substr(1, appropriate_links.size() - 2); // strip '[' and ']'

    // Special case for performance
    if (appropriate_links.find("fn:") == std::string::npos) {
        // No predicates so split on COMMA and return
        retval = split(appropriate_links, ',');
        return retval;
    }

    std::string s = appropriate_links;
    while (s.size() > 0) {
        if (s.rfind("fn:", 0) == 0) {
            // s starts with a predicate
            std::smatch     m;
            if (std::regex_search(s, m, r_Links) && m.ready() && (m.size() == 4)) {
                // m[2] = PDF version "x.y" --> convert to integer as x*10 + y
                assert(m[2].str().size() == 3);
                int arl_v = ((m[2].str()[0] - '0') * 10) + (m[2].str()[2] - '0');
                assert((arl_v >= 10) && ((arl_v <= 17) || (arl_v == 20)));

                // m[1] = predicate function name (no "fn:")
                if (((m[1] == "SinceVersion") && (pdf_version >= arl_v)) ||
                    ((m[1] == "BeforeVersion") && (pdf_version < arl_v)) ||
                    ((m[1] == "IsPDFVersion") && (pdf_version == arl_v)) ||
                    ((m[1] == "Deprecated") && (pdf_version < arl_v))) {
                    // m[3] = Arlington link
                    retval.push_back(m[3]);
                }
                s = m.suffix();
                if (s[0] == ',')
                    s = s.substr(1);
            }
            else {
                assert(false && "unexpected predicate in Arlington Links!");
                s.clear();
            }
        }
        else if (s.find(',') != std::string::npos) {
            // Next in list doesn't have a predicate
            auto i = s.find(',');
            retval.push_back(s.substr(0, i));
            s = s.substr(i + 1);
        }
        else {
            // Must be the last thing in the list (no trailing COMMA)
            assert((s.find(',') == std::string::npos) && (s.find("fn") == std::string::npos));
            retval.push_back(s);
            s.clear();
        }
    } // while

    return retval;
}


/// @brief Return the full Arlington Link set AFTER blindly removing predicates (i.e. ignore current PDF version).
///
/// @param[in] arl_links   a raw Arlington 'Links' field, including complex ([];[];[]) and predicates
///
/// @returns a simplified but full set (vector) of Arlington Links appropriate for the type of PDF object.
/// Or empty vector if nothing appropriate.
std::vector<std::string>  ArlVersion::get_full_linkset(std::string arl_links) {
    std::vector<std::string>      retval;

    if ((arl_type_index < 0) || (arl_links == ""))
        return retval; // empty vector of strings (i.e. no Links)

    // Brute force removal of all predicates
    std::string s = remove_type_link_predicates(arl_links);

    std::vector<std::string>    links = split(s, ';');
    assert(arl_type_index < (int)links.size());
    std::string                 full_links = links[arl_type_index];
    assert(full_links[0] == '[');
    full_links = full_links.substr(1, full_links.size() - 2); // strip '[' and ']'
    retval = split(full_links, ',');
    return retval;
}
