#include "doctest.h"

#include <cmath>
#include <stdexcept>

#include "domain/money.hpp"
#include "domain/text_fold.hpp"
#include "houses/feature.hpp"

using namespace wcatalog;

TEST_CASE("fold_text: minusculas y sin tildes") {
    CHECK(fold_text("Vedado") == "vedado");
    CHECK(fold_text("Pe\xC3\xB1" "a") == "pena");
    CHECK(fold_text("\xC3\xA1\xC3\xA9\xC3\xAD\xC3\xB3\xC3\xBA\xC3\xBC\xC3\xB1") == "aeiouun");
    CHECK(fold_text("\xC3\x81\xC3\x89\xC3\x8D\xC3\x93\xC3\x9A\xC3\x9C\xC3\x91") == "aeiouun");
    CHECK(fold_text("CAMI\xC3\x93N A\xC3\x91O") == "camion ano");
    CHECK(fold_text("Playa del Cari\xC3\xB1o 123") == "playa del carino 123");
    CHECK(fold_text("") == "");
}

TEST_CASE("fold_text: otras letras latinas y texto no latino") {
    CHECK(fold_text("\xC3\x87" "a\xC3\xA7" "ador") == "cacador");
    CHECK(fold_text("\xC5\x81\xC3\xB3" "d\xC5\xBA") == "lodz");
    CHECK(fold_text("stra\xC3\x9F" "e") == "strasse");
    CHECK(fold_text("\xE6\x97\xA5\xE6\x9C\xAC") == "\xE6\x97\xA5\xE6\x9C\xAC");
    CHECK(fold_text("a\xFF" "b") == "a\xFF" "b");
}

TEST_CASE("precio a centavos: redondeo y rechazo de negativos") {
    CHECK(price_to_cents(85000) == 8500000);
    CHECK(price_to_cents(0.005) == 1);
    CHECK(price_to_cents(1234.5) == 123450);
    CHECK(price_to_cents(0) == 0);
    CHECK(price_to_cents(1.005) == 101);
    CHECK(price_to_cents(19.999) == 2000);
    CHECK_THROWS_AS(price_to_cents(-0.01), std::invalid_argument);
    CHECK_THROWS_AS(price_to_cents(1e11), std::invalid_argument);
    CHECK_THROWS_AS(price_to_cents(std::nan("")), std::invalid_argument);
    CHECK(cents_to_price(123450) == doctest::Approx(1234.5));
    CHECK(cents_to_price(price_to_cents(85000.12)) == doctest::Approx(85000.12));
}

namespace {

Feature make(const std::string& type, std::vector<std::string> options = {}, const std::string& unit = "") {
    Feature f;
    f.label = "Prueba";
    f.type = type;
    f.options = std::move(options);
    f.unit = unit;
    return f;
}

}  // namespace

TEST_CASE("valor bool") {
    Feature f = make("bool");
    CHECK(*serialize_feature_value(f, true) == "1");
    CHECK(*serialize_feature_value(f, false) == "0");
    CHECK(*serialize_feature_value(f, "1") == "1");
    CHECK_FALSE(serialize_feature_value(f, nullptr).has_value());
    CHECK_THROWS_AS(serialize_feature_value(f, "quizas"), ValueError);
    CHECK_THROWS_AS(serialize_feature_value(f, 5), ValueError);
    CHECK(deserialize_feature_value(f, "1") == true);
    CHECK(display_feature_value(f, "1") == "S\xC3\xAD");
}

TEST_CASE("valor numero: decimales, texto numerico, negativos y basura") {
    Feature f = make("numero", {}, "m\xC2\xB2");
    CHECK(*serialize_feature_value(f, 2) == "2");
    CHECK(*serialize_feature_value(f, 2.5) == "2.5");
    CHECK(*serialize_feature_value(f, "1995") == "1995");
    CHECK_FALSE(serialize_feature_value(f, "  ").has_value());
    CHECK_THROWS_AS(serialize_feature_value(f, -1), ValueError);
    CHECK_THROWS_AS(serialize_feature_value(f, "abc"), ValueError);
    CHECK_THROWS_AS(serialize_feature_value(f, "12abc"), ValueError);
    CHECK_THROWS_AS(serialize_feature_value(f, 1e12), ValueError);
    CHECK_THROWS_AS(serialize_feature_value(f, true), ValueError);
    CHECK(display_feature_value(f, "2.5") == "2.5 m\xC2\xB2");
    CHECK(deserialize_feature_value(f, "2.5") == 2.5);
}

TEST_CASE("valor texto") {
    Feature f = make("texto");
    CHECK(*serialize_feature_value(f, "  Vista al mar ") == "Vista al mar");
    CHECK_FALSE(serialize_feature_value(f, "   ").has_value());
    CHECK_THROWS_AS(serialize_feature_value(f, 3), ValueError);
    CHECK_THROWS_AS(serialize_feature_value(f, std::string(501, 'x')), ValueError);
}

TEST_CASE("valor opcion unica: solo opciones de la lista") {
    Feature f = make("opcion", {"Fibra", "ADSL"});
    CHECK(*serialize_feature_value(f, "Fibra") == "Fibra");
    CHECK_FALSE(serialize_feature_value(f, "").has_value());
    try {
        serialize_feature_value(f, "Satelite");
        FAIL("debia rechazar la opcion");
    } catch (const ValueError& e) {
        CHECK(std::string(e.what()).find("Satelite") != std::string::npos);
    }
    CHECK_THROWS_AS(serialize_feature_value(f, Json::array({"Fibra"})), ValueError);
}

TEST_CASE("valor multiopcion: arreglo JSON sin repetidos") {
    Feature f = make("multiopcion", {"Rejas", "C\xC3\xA1maras", "Alarma"});
    CHECK(*serialize_feature_value(f, Json::array({"C\xC3\xA1maras", "Rejas", "Rejas"})) ==
          "[\"C\xC3\xA1maras\",\"Rejas\"]");
    CHECK_FALSE(serialize_feature_value(f, Json::array()).has_value());
    CHECK_THROWS_AS(serialize_feature_value(f, Json::array({"Rejas", "Fuego"})), ValueError);
    CHECK_THROWS_AS(serialize_feature_value(f, "Rejas"), ValueError);
    CHECK(deserialize_feature_value(f, "[\"Rejas\",\"Alarma\"]") == Json::array({"Rejas", "Alarma"}));
    CHECK(display_feature_value(f, "[\"Rejas\",\"Alarma\"]") == "Rejas, Alarma");
}
