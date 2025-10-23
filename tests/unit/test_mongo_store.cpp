#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "mongo_store.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::NiceMock;
using ::testing::DoAll;
using ::testing::Invoke;

// -----------------------------------------------------------------------------
// Mock classes to simulate MongoDB collection behavior
// -----------------------------------------------------------------------------
class MockMongoCollection {
public:
    MOCK_METHOD((void), insert_one, (const bsoncxx::document::view&), ());
    MOCK_METHOD((bsoncxx::document::value), find_one, (const bsoncxx::document::view&), ());
};

// -----------------------------------------------------------------------------
// Fake MongoStore subclass that uses mocked collection instead of real DB
// -----------------------------------------------------------------------------
class FakeMongoStore : public MongoStore {
public:
    explicit FakeMongoStore(MockMongoCollection* mockColl)
        : MongoStore("mongodb://fake-uri-for-tests"), mock_collection(mockColl) {}

    // Override internal calls to redirect to mock collection
    void add_event(const TransitEvent& e) override {
        bsoncxx::builder::basic::document doc;
        doc.append(bsoncxx::builder::basic::kvp("user_id", e.user_id));
        doc.append(bsoncxx::builder::basic::kvp("mode", e.mode));
        doc.append(bsoncxx::builder::basic::kvp("distance_km", e.distance_km));
        doc.append(bsoncxx::builder::basic::kvp("ts", e.ts));
        mock_collection->insert_one(doc.view());
    }

    std::vector<TransitEvent> get_events(const std::string& user) const override {
        // Simulate a document being returned from Mongo
        bsoncxx::builder::basic::document doc;
        doc.append(bsoncxx::builder::basic::kvp("user_id", user));
        doc.append(bsoncxx::builder::basic::kvp("mode", "car"));
        doc.append(bsoncxx::builder::basic::kvp("distance_km", 12.0));
        doc.append(bsoncxx::builder::basic::kvp("ts", 12345));
        bsoncxx::document::value fake_doc = doc.extract();
        mock_collection->find_one(fake_doc.view());

        TransitEvent ev;
        ev.user_id = user;
        ev.mode = "car";
        ev.distance_km = 12.0;
        ev.ts = 12345;
        return { ev };
    }

private:
    MockMongoCollection* mock_collection;
};

// -----------------------------------------------------------------------------
// Test Fixture
// -----------------------------------------------------------------------------
struct MongoStoreTest : public ::testing::Test {
protected:
    NiceMock<MockMongoCollection> mockColl;
    std::unique_ptr<FakeMongoStore> store;

    void SetUp() override {
        std::cout << "[SetUp] Initializing FakeMongoStore with mock collection..." << std::endl;
        store = std::make_unique<FakeMongoStore>(&mockColl);
    }

    void TearDown() override {
        std::cout << "[TearDown] Cleaning up FakeMongoStore..." << std::endl;
        store.reset();
    }
};

// -----------------------------------------------------------------------------
// Tests
// -----------------------------------------------------------------------------
TEST_F(MongoStoreTest, AddEvent_CallsInsertOneWithCorrectFields) {
    TransitEvent ev{ "user1", "car", 15.5, 1234567 };

    EXPECT_CALL(mockColl, insert_one(_))
        .Times(1);

    store->add_event(ev);
}

TEST_F(MongoStoreTest, GetEvents_ReturnsValidTransitEvent) {
    EXPECT_CALL(mockColl, find_one(_))
        .Times(1);

    auto events = store->get_events("user2");

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].user_id, "user2");
    EXPECT_EQ(events[0].mode, "car");
    EXPECT_DOUBLE_EQ(events[0].distance_km, 12.0);
    EXPECT_EQ(events[0].ts, 12345);
}
