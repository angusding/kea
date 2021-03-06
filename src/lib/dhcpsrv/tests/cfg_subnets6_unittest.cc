// Copyright (C) 2014-2018 Internet Systems Consortium, Inc. ("ISC")
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <config.h>
#include <dhcp/classify.h>
#include <dhcp/dhcp6.h>
#include <dhcp/option.h>
#include <dhcp/option_string.h>
#include <dhcpsrv/cfg_shared_networks.h>
#include <dhcpsrv/cfg_subnets6.h>
#include <dhcpsrv/subnet.h>
#include <dhcpsrv/subnet_id.h>
#include <dhcpsrv/subnet_selector.h>
#include <testutils/test_to_element.h>
#include <gtest/gtest.h>
#include <string>

using namespace isc;
using namespace isc::asiolink;
using namespace isc::dhcp;
using namespace isc::test;

namespace {

/// @brief Generates interface id option.
///
/// @param text Interface id in a textual format.
OptionPtr
generateInterfaceId(const std::string& text) {
    OptionBuffer buffer(text.begin(), text.end());
    return OptionPtr(new Option(Option::V6, D6O_INTERFACE_ID, buffer));
}

/// @brief Verifies that a set of subnets contains a given a subnet
///
/// @param cfg_subnets set of sunbets in which to look
/// @param exp_subnet_id expected id of the target subnet
/// @param prefix prefix of the target subnet
/// @param exp_valid expected valid lifetime of the subnet
/// @param exp_network  pointer to the subnet's shared-network (if one)
void checkMergedSubnet(CfgSubnets6& cfg_subnets,
                       const std::string& prefix,
                       const SubnetID exp_subnet_id,
                       int exp_valid,
                       SharedNetwork6Ptr exp_network) {
    // Look for the network by prefix.
    auto subnet = cfg_subnets.getByPrefix(prefix);
    ASSERT_TRUE(subnet) << "subnet: " << prefix << " not found";

    // Make sure we have the one we expect.
    ASSERT_EQ(exp_subnet_id, subnet->getID()) << "subnet ID is wrong";
    ASSERT_EQ(exp_valid, subnet->getValid()) << "subnetID:"
              << subnet->getID() << ", subnet valid time is wrong";

    SharedNetwork6Ptr shared_network;
    subnet->getSharedNetwork(shared_network);
    if (exp_network) {
        ASSERT_TRUE(shared_network)
            << " expected network: " << exp_network->getName() << " not found";
        ASSERT_TRUE(shared_network == exp_network) << " networks do no match";
    } else {
        ASSERT_FALSE(shared_network) << " unexpected network assignment: "
            << shared_network->getName();
    }
}

// This test verifies that specific subnet can be retrieved by specifying
// subnet identifier or subnet prefix.
TEST(CfgSubnets6Test, getSpecificSubnet) {
    CfgSubnets6 cfg;

    // Create 3 subnets.
    Subnet6Ptr subnet1(new Subnet6(IOAddress("2001:db8:1::"), 48, 1, 2, 3, 4,
                                   SubnetID(5)));
    Subnet6Ptr subnet2(new Subnet6(IOAddress("2001:db8:2::"), 48, 1, 2, 3, 4,
                                   SubnetID(8)));
    Subnet6Ptr subnet3(new Subnet6(IOAddress("2001:db8:3::"), 48, 1, 2, 3, 4,
                                   SubnetID(10)));

    // Store the subnets in a vector to make it possible to loop over
    // all configured subnets.
    std::vector<Subnet6Ptr> subnets;
    subnets.push_back(subnet1);
    subnets.push_back(subnet2);
    subnets.push_back(subnet3);

    // Add all subnets to the configuration.
    for (auto subnet = subnets.cbegin(); subnet != subnets.cend(); ++subnet) {
        ASSERT_NO_THROW(cfg.add(*subnet)) << "failed to add subnet with id: "
            << (*subnet)->getID();
    }

    // Iterate over all subnets and make sure they can be retrieved by
    // subnet identifier.
    for (auto subnet = subnets.rbegin(); subnet != subnets.rend(); ++subnet) {
        ConstSubnet6Ptr subnet_returned = cfg.getBySubnetId((*subnet)->getID());
        ASSERT_TRUE(subnet_returned) << "failed to return subnet with id: "
            << (*subnet)->getID();
        EXPECT_EQ((*subnet)->getID(), subnet_returned->getID());
        EXPECT_EQ((*subnet)->toText(), subnet_returned->toText());
    }

    // Repeat the previous test, but this time retrieve subnets by their
    // prefixes.
    for (auto subnet = subnets.rbegin(); subnet != subnets.rend(); ++subnet) {
        ConstSubnet6Ptr subnet_returned = cfg.getByPrefix((*subnet)->toText());
        ASSERT_TRUE(subnet_returned) << "failed to return subnet with id: "
            << (*subnet)->getID();
        EXPECT_EQ((*subnet)->getID(), subnet_returned->getID());
        EXPECT_EQ((*subnet)->toText(), subnet_returned->toText());
    }

    // Make sure that null pointers are returned for non-existing subnets.
    EXPECT_FALSE(cfg.getBySubnetId(SubnetID(123)));
    EXPECT_FALSE(cfg.getByPrefix("3000::/16"));
}

// This test verifies that a single subnet can be removed from the configuration.
TEST(CfgSubnets6Test, deleteSubnet) {
    CfgSubnets6 cfg;

    // Create 3 subnets.
    Subnet6Ptr subnet1(new Subnet6(IOAddress("2001:db8:1::"), 48, 1, 2, 3, 4));
    Subnet6Ptr subnet2(new Subnet6(IOAddress("2001:db8:2::"), 48, 1, 2, 3, 4));
    Subnet6Ptr subnet3(new Subnet6(IOAddress("2001:db8:3::"), 48, 1, 2, 3, 4));

    ASSERT_NO_THROW(cfg.add(subnet1));
    ASSERT_NO_THROW(cfg.add(subnet2));
    ASSERT_NO_THROW(cfg.add(subnet3));

    // There should be three subnets.
    ASSERT_EQ(3, cfg.getAll()->size());
    // We're going to remove the subnet #2. Let's make sure it exists before
    // we remove it.
    ASSERT_TRUE(cfg.getByPrefix("2001:db8:2::/48"));

    // Remove the subnet and make sure it is gone.
    ASSERT_NO_THROW(cfg.del(subnet2));
    ASSERT_EQ(2, cfg.getAll()->size());
    EXPECT_FALSE(cfg.getByPrefix("2001:db8:2::/48"));

    // Remove another subnet by ID.
    ASSERT_NO_THROW(cfg.del(subnet1->getID()));
    ASSERT_EQ(1, cfg.getAll()->size());
    EXPECT_FALSE(cfg.getByPrefix("2001:db8:1::/48"));
}

// This test checks that the subnet can be selected using a relay agent's
// link address.
TEST(CfgSubnets6Test, selectSubnetByRelayAddress) {
    CfgSubnets6 cfg;

    // Let's configure 3 subnets
    Subnet6Ptr subnet1(new Subnet6(IOAddress("2001:db8:1::"), 48, 1, 2, 3, 4));
    Subnet6Ptr subnet2(new Subnet6(IOAddress("2001:db8:2::"), 48, 1, 2, 3, 4));
    Subnet6Ptr subnet3(new Subnet6(IOAddress("2001:db8:3::"), 48, 1, 2, 3, 4));
    cfg.add(subnet1);
    cfg.add(subnet2);
    cfg.add(subnet3);

    // Make sure that none of the subnets is selected when there is no relay
    // information configured for them.
    SubnetSelector selector;
    selector.first_relay_linkaddr_ = IOAddress("2001:db8:ff::1");
    EXPECT_FALSE(cfg.selectSubnet(selector));
    selector.first_relay_linkaddr_ = IOAddress("2001:db8:ff::2");
    EXPECT_FALSE(cfg.selectSubnet(selector));
    selector.first_relay_linkaddr_ = IOAddress("2001:db8:ff::3");
    EXPECT_FALSE(cfg.selectSubnet(selector));

    // Now specify relay information.
    subnet1->addRelayAddress(IOAddress("2001:db8:ff::1"));
    subnet2->addRelayAddress(IOAddress("2001:db8:ff::2"));
    subnet3->addRelayAddress(IOAddress("2001:db8:ff::3"));

    // And try again. This time relay-info is there and should match.
    selector.first_relay_linkaddr_ = IOAddress("2001:db8:ff::1");
    EXPECT_EQ(subnet1, cfg.selectSubnet(selector));
    selector.first_relay_linkaddr_ = IOAddress("2001:db8:ff::2");
    EXPECT_EQ(subnet2, cfg.selectSubnet(selector));
    selector.first_relay_linkaddr_ = IOAddress("2001:db8:ff::3");
    EXPECT_EQ(subnet3, cfg.selectSubnet(selector));
}

// This test checks that the subnet can be selected using an interface
// name associated with a asubnet.
TEST(CfgSubnets6Test, selectSubnetByInterfaceName) {
    CfgSubnets6 cfg;

    // Let's create 3 subnets.
    Subnet6Ptr subnet1(new Subnet6(IOAddress("2000::"), 48, 1, 2, 3, 4));
    Subnet6Ptr subnet2(new Subnet6(IOAddress("3000::"), 48, 1, 2, 3, 4));
    Subnet6Ptr subnet3(new Subnet6(IOAddress("4000::"), 48, 1, 2, 3, 4));
    subnet1->setIface("foo");
    subnet2->setIface("bar");
    subnet3->setIface("foobar");

    // Until subnets are added to the configuration, there should be nothing
    // returned.
    SubnetSelector selector;
    selector.iface_name_ = "foo";
    EXPECT_FALSE(cfg.selectSubnet(selector));

    // Add one of the subnets.
    cfg.add(subnet1);

    // The subnet should be now selected for the interface name "foo".
    EXPECT_EQ(subnet1, cfg.selectSubnet(selector));

    // Check that the interface name is checked even when there is
    // only one subnet defined: there should be nothing returned when
    // other interface name is specified.
    selector.iface_name_ = "bar";
    EXPECT_FALSE(cfg.selectSubnet(selector));

    // Add other subnets.
    cfg.add(subnet2);
    cfg.add(subnet3);

    // When we specify correct interface names, the subnets should be returned.
    selector.iface_name_ = "foobar";
    EXPECT_EQ(subnet3, cfg.selectSubnet(selector));
    selector.iface_name_ = "bar";
    EXPECT_EQ(subnet2, cfg.selectSubnet(selector));

    // When specifying a non-existing interface the subnet should not be
    // returned.
    selector.iface_name_ = "xyzzy";
    EXPECT_FALSE(cfg.selectSubnet(selector));
}

// This test checks that the subnet can be selected using an Interface ID
// option inserted by a relay.
TEST(CfgSubnets6Test, selectSubnetByInterfaceId) {
    CfgSubnets6 cfg;

    // Create 3 subnets.
    Subnet6Ptr subnet1(new Subnet6(IOAddress("2000::"), 48, 1, 2, 3, 4));
    Subnet6Ptr subnet2(new Subnet6(IOAddress("3000::"), 48, 1, 2, 3, 4));
    Subnet6Ptr subnet3(new Subnet6(IOAddress("4000::"), 48, 1, 2, 3, 4));

    // Create Interface-id options used in subnets 1,2, and 3
    OptionPtr ifaceid1 = generateInterfaceId("relay1.eth0");
    OptionPtr ifaceid2 = generateInterfaceId("VL32");
    // That's a strange interface-id, but this is a real life example
    OptionPtr ifaceid3 = generateInterfaceId("ISAM144|299|ipv6|nt:vp:1:110");

    // Bogus interface-id.
    OptionPtr ifaceid_bogus = generateInterfaceId("non-existent");

    // Assign interface ids to the respective subnets.
    subnet1->setInterfaceId(ifaceid1);
    subnet2->setInterfaceId(ifaceid2);
    subnet3->setInterfaceId(ifaceid3);

    // There shouldn't be any subnet configured at this stage.
    SubnetSelector selector;
    selector.interface_id_ = ifaceid1;
    // Note that some link address must be specified to indicate that it is
    // a relayed message!
    selector.first_relay_linkaddr_ = IOAddress("5000::1");
    EXPECT_FALSE(cfg.selectSubnet(selector));

    // Add one of the subnets.
    cfg.add(subnet1);

    // If only one subnet has been specified, it should be returned when the
    // interface id matches. But, for a different interface id there should be
    // no match.
    EXPECT_EQ(subnet1, cfg.selectSubnet(selector));
    selector.interface_id_ = ifaceid2;
    EXPECT_FALSE(cfg.selectSubnet(selector));

    // Add other subnets.
    cfg.add(subnet2);
    cfg.add(subnet3);

    // Now that we have all subnets added. we should be able to retrieve them
    // using appropriate interface ids.
    selector.interface_id_ = ifaceid3;
    EXPECT_EQ(subnet3, cfg.selectSubnet(selector));
    selector.interface_id_ = ifaceid2;
    EXPECT_EQ(subnet2, cfg.selectSubnet(selector));

    // For invalid interface id, there should be nothing returned.
    selector.interface_id_ = ifaceid_bogus;
    EXPECT_FALSE(cfg.selectSubnet(selector));
}

// Test that the client classes are considered when the subnet is selected by
// the relay link address.
TEST(CfgSubnets6Test, selectSubnetByRelayAddressAndClassify) {
    CfgSubnets6 cfg;

    // Let's configure 3 subnets
    Subnet6Ptr subnet1(new Subnet6(IOAddress("2000::"), 48, 1, 2, 3, 4));
    Subnet6Ptr subnet2(new Subnet6(IOAddress("3000::"), 48, 1, 2, 3, 4));
    Subnet6Ptr subnet3(new Subnet6(IOAddress("4000::"), 48, 1, 2, 3, 4));
    cfg.add(subnet1);
    cfg.add(subnet2);
    cfg.add(subnet3);

    // Let's sanity check that we can use that configuration.
    SubnetSelector selector;
    selector.first_relay_linkaddr_ = IOAddress("2000::123");
    EXPECT_EQ(subnet1, cfg.selectSubnet(selector));
    selector.first_relay_linkaddr_ = IOAddress("3000::345");
    EXPECT_EQ(subnet2, cfg.selectSubnet(selector));
    selector.first_relay_linkaddr_ = IOAddress("4000::567");
    EXPECT_EQ(subnet3, cfg.selectSubnet(selector));

    // Client now belongs to bar class.
    selector.client_classes_.insert("bar");

    // There are no class restrictions defined, so everything should work
    // as before.
    selector.first_relay_linkaddr_ = IOAddress("2000::123");
    EXPECT_EQ(subnet1, cfg.selectSubnet(selector));
    selector.first_relay_linkaddr_ = IOAddress("3000::345");
    EXPECT_EQ(subnet2, cfg.selectSubnet(selector));
    selector.first_relay_linkaddr_ = IOAddress("4000::567");
    EXPECT_EQ(subnet3, cfg.selectSubnet(selector));

    // Now let's add client class restrictions.
    subnet1->allowClientClass("foo"); // Serve here only clients from foo class
    subnet2->allowClientClass("bar"); // Serve here only clients from bar class
    subnet3->allowClientClass("baz"); // Serve here only clients from baz class

    // The same check as above should result in client being served only in
    // bar class, i.e. subnet2
    selector.first_relay_linkaddr_ = IOAddress("2000::123");
    EXPECT_FALSE(cfg.selectSubnet(selector));
    selector.first_relay_linkaddr_ = IOAddress("3000::345");
    EXPECT_EQ(subnet2, cfg.selectSubnet(selector));
    selector.first_relay_linkaddr_ = IOAddress("4000::567");
    EXPECT_FALSE(cfg.selectSubnet(selector));

    // Now let's check that client with wrong class is not supported
    selector.client_classes_.clear();
    selector.client_classes_.insert("some_other_class");
    selector.first_relay_linkaddr_ = IOAddress("2000::123");
    EXPECT_FALSE(cfg.selectSubnet(selector));
    selector.first_relay_linkaddr_ = IOAddress("3000::345");
    EXPECT_FALSE(cfg.selectSubnet(selector));
    selector.first_relay_linkaddr_ = IOAddress("4000::567");
    EXPECT_FALSE(cfg.selectSubnet(selector));

    // Finally, let's check that client without any classes is not supported
    selector.client_classes_.clear();
    selector.first_relay_linkaddr_ = IOAddress("2000::123");
    EXPECT_FALSE(cfg.selectSubnet(selector));
    selector.first_relay_linkaddr_ = IOAddress("3000::345");
    EXPECT_FALSE(cfg.selectSubnet(selector));
    selector.first_relay_linkaddr_ = IOAddress("4000::567");
    EXPECT_FALSE(cfg.selectSubnet(selector));
}

// Test that client classes are considered when the subnet is selected by the
// interface name.
TEST(CfgSubnets6Test, selectSubnetByInterfaceNameAndClassify) {
    CfgSubnets6 cfg;

    Subnet6Ptr subnet1(new Subnet6(IOAddress("2000::"), 48, 1, 2, 3, 4));
    Subnet6Ptr subnet2(new Subnet6(IOAddress("3000::"), 48, 1, 2, 3, 4));
    Subnet6Ptr subnet3(new Subnet6(IOAddress("4000::"), 48, 1, 2, 3, 4));
    subnet1->setIface("foo");
    subnet2->setIface("bar");
    subnet3->setIface("foobar");

    cfg.add(subnet1);
    cfg.add(subnet2);
    cfg.add(subnet3);

    // Now we have only one subnet, any request will be served from it
    SubnetSelector selector;
    selector.client_classes_.insert("bar");
    selector.iface_name_ = "foo";
    EXPECT_EQ(subnet1, cfg.selectSubnet(selector));
    selector.iface_name_ = "bar";
    EXPECT_EQ(subnet2, cfg.selectSubnet(selector));
    selector.iface_name_ = "foobar";
    EXPECT_EQ(subnet3, cfg.selectSubnet(selector));

    subnet1->allowClientClass("foo"); // Serve here only clients from foo class
    subnet2->allowClientClass("bar"); // Serve here only clients from bar class
    subnet3->allowClientClass("baz"); // Serve here only clients from baz class

    selector.iface_name_ = "foo";
    EXPECT_FALSE(cfg.selectSubnet(selector));
    selector.iface_name_ = "bar";
    EXPECT_EQ(subnet2, cfg.selectSubnet(selector));
    selector.iface_name_ = "foobar";
    EXPECT_FALSE(cfg.selectSubnet(selector));
}

// Test that client classes are considered when the interface is selected by
// the interface id.
TEST(CfgSubnets6Test, selectSubnetByInterfaceIdAndClassify) {
    CfgSubnets6 cfg;

    Subnet6Ptr subnet1(new Subnet6(IOAddress("2000::"), 48, 1, 2, 3, 4));
    Subnet6Ptr subnet2(new Subnet6(IOAddress("3000::"), 48, 1, 2, 3, 4));
    Subnet6Ptr subnet3(new Subnet6(IOAddress("4000::"), 48, 1, 2, 3, 4));

    // interface-id options used in subnets 1,2, and 3
    OptionPtr ifaceid1 = generateInterfaceId("relay1.eth0");
    OptionPtr ifaceid2 = generateInterfaceId("VL32");
    // That's a strange interface-id, but this is a real life example
    OptionPtr ifaceid3 = generateInterfaceId("ISAM144|299|ipv6|nt:vp:1:110");

    // bogus interface-id
    OptionPtr ifaceid_bogus = generateInterfaceId("non-existent");

    subnet1->setInterfaceId(ifaceid1);
    subnet2->setInterfaceId(ifaceid2);
    subnet3->setInterfaceId(ifaceid3);

    cfg.add(subnet1);
    cfg.add(subnet2);
    cfg.add(subnet3);

    // If we have only a single subnet and the request came from a local
    // address, let's use that subnet
    SubnetSelector selector;
    selector.first_relay_linkaddr_ = IOAddress("5000::1");
    selector.client_classes_.insert("bar");
    selector.interface_id_ = ifaceid1;
    EXPECT_EQ(subnet1, cfg.selectSubnet(selector));
    selector.interface_id_ = ifaceid2;
    EXPECT_EQ(subnet2, cfg.selectSubnet(selector));
    selector.interface_id_ = ifaceid3;
    EXPECT_EQ(subnet3, cfg.selectSubnet(selector));

    subnet1->allowClientClass("foo"); // Serve here only clients from foo class
    subnet2->allowClientClass("bar"); // Serve here only clients from bar class
    subnet3->allowClientClass("baz"); // Serve here only clients from baz class

    EXPECT_FALSE(cfg.selectSubnet(selector));
    selector.interface_id_ = ifaceid2;
    EXPECT_EQ(subnet2, cfg.selectSubnet(selector));
    selector.interface_id_ = ifaceid3;
    EXPECT_FALSE(cfg.selectSubnet(selector));
}

// Checks that detection of duplicated subnet IDs works as expected. It should
// not be possible to add two IPv6 subnets holding the same ID.
TEST(CfgSubnets6Test, duplication) {
    CfgSubnets6 cfg;

    Subnet6Ptr subnet1(new Subnet6(IOAddress("2000::"), 48, 1, 2, 3, 4, 123));
    Subnet6Ptr subnet2(new Subnet6(IOAddress("3000::"), 48, 1, 2, 3, 4, 124));
    Subnet6Ptr subnet3(new Subnet6(IOAddress("4000::"), 48, 1, 2, 3, 4, 123));

    ASSERT_NO_THROW(cfg.add(subnet1));
    EXPECT_NO_THROW(cfg.add(subnet2));
    // Subnet 3 has the same ID as subnet 1. It shouldn't be able to add it.
    EXPECT_THROW(cfg.add(subnet3), isc::dhcp::DuplicateSubnetID);
}

// This test check if IPv6 subnets can be unparsed in a predictable way,
TEST(CfgSubnets6Test, unparseSubnet) {
    CfgSubnets6 cfg;

    // Add some subnets.
    Subnet6Ptr subnet1(new Subnet6(IOAddress("2001:db8:1::"),
                                   48, 1, 2, 3, 4, 123));
    Subnet6Ptr subnet2(new Subnet6(IOAddress("2001:db8:2::"),
                                   48, 1, 2, 3, 4, 124));
    Subnet6Ptr subnet3(new Subnet6(IOAddress("2001:db8:3::"),
                                   48, 1, 2, 3, 4, 125));

    OptionPtr ifaceid = generateInterfaceId("relay.eth0");
    subnet1->setInterfaceId(ifaceid);
    subnet1->allowClientClass("foo");
    subnet2->setIface("lo");
    subnet2->addRelayAddress(IOAddress("2001:db8:ff::2"));
    subnet3->setIface("eth1");
    subnet3->requireClientClass("foo");
    subnet3->requireClientClass("bar");
    subnet3->setHostReservationMode(Network::HR_ALL);
    subnet3->setRapidCommit(false);

    data::ElementPtr ctx1 = data::Element::fromJSON("{ \"comment\": \"foo\" }");
    subnet1->setContext(ctx1);
    data::ElementPtr ctx2 = data::Element::createMap();
    subnet2->setContext(ctx2);

    cfg.add(subnet1);
    cfg.add(subnet2);
    cfg.add(subnet3);

    // Unparse
    std::string expected = "[\n"
        "{\n"
        "    \"comment\": \"foo\",\n"
        "    \"id\": 123,\n"
        "    \"subnet\": \"2001:db8:1::/48\",\n"
        "    \"interface-id\": \"relay.eth0\",\n"
        "    \"renew-timer\": 1,\n"
        "    \"rebind-timer\": 2,\n"
        "    \"relay\": { \"ip-addresses\": [ ] },\n"
        "    \"preferred-lifetime\": 3,\n"
        "    \"valid-lifetime\": 4,\n"
        "    \"client-class\": \"foo\",\n"
        "    \"pools\": [ ],\n"
        "    \"pd-pools\": [ ],\n"
        "    \"option-data\": [ ]\n"
        "},{\n"
        "    \"id\": 124,\n"
        "    \"subnet\": \"2001:db8:2::/48\",\n"
        "    \"interface\": \"lo\",\n"
        "    \"renew-timer\": 1,\n"
        "    \"rebind-timer\": 2,\n"
        "    \"relay\": { \"ip-addresses\": [ \"2001:db8:ff::2\" ] },\n"
        "    \"preferred-lifetime\": 3,\n"
        "    \"valid-lifetime\": 4,\n"
        "    \"user-context\": { },\n"
        "    \"pools\": [ ],\n"
        "    \"pd-pools\": [ ],\n"
        "    \"option-data\": [ ]\n"
        "},{\n"
        "    \"id\": 125,\n"
        "    \"subnet\": \"2001:db8:3::/48\",\n"
        "    \"interface\": \"eth1\",\n"
        "    \"renew-timer\": 1,\n"
        "    \"rebind-timer\": 2,\n"
        "    \"relay\": { \"ip-addresses\": [ ] },\n"
        "    \"preferred-lifetime\": 3,\n"
        "    \"valid-lifetime\": 4,\n"
        "    \"rapid-commit\": false,\n"
        "    \"reservation-mode\": \"all\",\n"
        "    \"pools\": [ ],\n"
        "    \"pd-pools\": [ ],\n"
        "    \"option-data\": [ ],\n"
        "    \"require-client-classes\": [ \"foo\", \"bar\" ]\n"
        "} ]\n";
    runToElementTest<CfgSubnets6>(expected, cfg);
}

// This test check if IPv6 pools can be unparsed in a predictable way,
TEST(CfgSubnets6Test, unparsePool) {
    CfgSubnets6 cfg;

    // Add a subnet with pools
    Subnet6Ptr subnet(new Subnet6(IOAddress("2001:db8:1::"),
                                  48, 1, 2, 3, 4, 123));
    Pool6Ptr pool1(new Pool6(Lease::TYPE_NA,
                             IOAddress("2001:db8:1::100"),
                             IOAddress("2001:db8:1::199")));
    Pool6Ptr pool2(new Pool6(Lease::TYPE_NA, IOAddress("2001:db8:1:1::"), 64));
    pool2->allowClientClass("bar");

    std::string json1 = "{ \"comment\": \"foo\", \"version\": 1 }";
    data::ElementPtr ctx1 = data::Element::fromJSON(json1);
    pool1->setContext(ctx1);
    data::ElementPtr ctx2 = data::Element::fromJSON("{ \"foo\": \"bar\" }");
    pool2->setContext(ctx2);
    pool2->requireClientClass("foo");

    subnet->addPool(pool1);
    subnet->addPool(pool2);
    cfg.add(subnet);

    // Unparse
    std::string expected = "[\n"
        "{\n"
        "    \"id\": 123,\n"
        "    \"subnet\": \"2001:db8:1::/48\",\n"
        "    \"renew-timer\": 1,\n"
        "    \"rebind-timer\": 2,\n"
        "    \"relay\": { \"ip-addresses\": [ ] },\n"
        "    \"preferred-lifetime\": 3,\n"
        "    \"valid-lifetime\": 4,\n"
        "    \"pools\": [\n"
        "        {\n"
        "            \"comment\": \"foo\",\n"
        "            \"pool\": \"2001:db8:1::100-2001:db8:1::199\",\n"
        "            \"user-context\": { \"version\": 1 },\n"
        "            \"option-data\": [ ]\n"
        "        },{\n"
        "            \"pool\": \"2001:db8:1:1::/64\",\n"
        "            \"user-context\": { \"foo\": \"bar\" },\n"
        "            \"option-data\": [ ],\n"
        "            \"client-class\": \"bar\",\n"
        "            \"require-client-classes\": [ \"foo\" ]\n"
        "        }\n"
        "    ],\n"
        "    \"pd-pools\": [ ],\n"
        "    \"option-data\": [ ]\n"
        "} ]\n";
    runToElementTest<CfgSubnets6>(expected, cfg);
}

// This test check if IPv6 prefix delegation pools can be unparsed
// in a predictable way,
TEST(CfgSubnets6Test, unparsePdPool) {
    CfgSubnets6 cfg;

    // Add a subnet with pd-pools
    Subnet6Ptr subnet(new Subnet6(IOAddress("2001:db8:1::"),
                                  48, 1, 2, 3, 4, 123));
    Pool6Ptr pdpool1(new Pool6(Lease::TYPE_PD,
                               IOAddress("2001:db8:2::"), 48, 64));
    Pool6Ptr pdpool2(new Pool6(IOAddress("2001:db8:3::"), 48, 56,
                               IOAddress("2001:db8:3::"), 64));
    pdpool2->allowClientClass("bar");

    data::ElementPtr ctx1 = data::Element::fromJSON("{ \"foo\": [ \"bar\" ] }");
    pdpool1->setContext(ctx1);
    pdpool1->requireClientClass("bar");
    pdpool2->allowClientClass("bar");

    subnet->addPool(pdpool1);
    subnet->addPool(pdpool2);
    cfg.add(subnet);

    // Unparse
    std::string expected = "[\n"
        "{\n"
        "    \"id\": 123,\n"
        "    \"subnet\": \"2001:db8:1::/48\",\n"
        "    \"renew-timer\": 1,\n"
        "    \"rebind-timer\": 2,\n"
        "    \"relay\": { \"ip-addresses\": [ ] },\n"
        "    \"preferred-lifetime\": 3,\n"
        "    \"valid-lifetime\": 4,\n"
        "    \"pools\": [ ],\n"
        "    \"pd-pools\": [\n"
        "        {\n"
        "            \"prefix\": \"2001:db8:2::\",\n"
        "            \"prefix-len\": 48,\n"
        "            \"delegated-len\": 64,\n"
        "            \"user-context\": { \"foo\": [ \"bar\" ] },\n"
        "            \"option-data\": [ ],\n"
        "            \"require-client-classes\": [ \"bar\" ]\n"
        "        },{\n"
        "            \"prefix\": \"2001:db8:3::\",\n"
        "            \"prefix-len\": 48,\n"
        "            \"delegated-len\": 56,\n"
        "            \"excluded-prefix\": \"2001:db8:3::\",\n"
        "            \"excluded-prefix-len\": 64,\n"
        "            \"option-data\": [ ],\n"
        "            \"client-class\": \"bar\"\n"
        "        }\n"
        "    ],\n"
        "    \"option-data\": [ ]\n"
        "} ]\n";
    runToElementTest<CfgSubnets6>(expected, cfg);
}

// This test verifies that it is possible to retrieve a subnet using subnet-id.
TEST(CfgSubnets6Test, getSubnet) {
    CfgSubnets6 cfg;

    // Let's configure 3 subnets
    Subnet6Ptr subnet1(new Subnet6(IOAddress("2001:db8:1::"), 48, 1, 2, 3, 4, 100));
    Subnet6Ptr subnet2(new Subnet6(IOAddress("2001:db8:2::"), 48, 1, 2, 3, 4, 200));
    Subnet6Ptr subnet3(new Subnet6(IOAddress("2001:db8:3::"), 48, 1, 2, 3, 4, 300));
    cfg.add(subnet1);
    cfg.add(subnet2);
    cfg.add(subnet3);

    EXPECT_EQ(subnet1, cfg.getSubnet(100));
    EXPECT_EQ(subnet2, cfg.getSubnet(200));
    EXPECT_EQ(subnet3, cfg.getSubnet(300));
    EXPECT_EQ(Subnet6Ptr(), cfg.getSubnet(400)); // no such subnet
}

// This test verifies that subnets configuration is properly merged.
TEST(CfgSubnets6Test, mergeSubnets) {
    // Create custom options dictionary for testing merge. We're keeping it
    // simple because they are more rigorous tests elsewhere.
    CfgOptionDefPtr cfg_def(new CfgOptionDef());
    cfg_def->add((OptionDefinitionPtr(new OptionDefinition("one", 1, "string"))), "isc");

    Subnet6Ptr subnet1(new Subnet6(IOAddress("2001:1::"),
                                   64, 1, 2, 100, 100, SubnetID(1)));
    Subnet6Ptr subnet2(new Subnet6(IOAddress("2001:2::"),
                                   64, 1, 2, 100, 100, SubnetID(2)));
    Subnet6Ptr subnet3(new Subnet6(IOAddress("2001:3::"),
                                   64, 1, 2, 100, 100, SubnetID(3)));
    Subnet6Ptr subnet4(new Subnet6(IOAddress("2001:4::"),
                                   64, 1, 2, 100, 100, SubnetID(4)));

    // Create the "existing" list of shared networks
    CfgSharedNetworks6Ptr networks(new CfgSharedNetworks6());
    SharedNetwork6Ptr shared_network1(new SharedNetwork6("shared-network1"));
    networks->add(shared_network1);
    SharedNetwork6Ptr shared_network2(new SharedNetwork6("shared-network2"));
    networks->add(shared_network2);

    // Empty network pointer.
    SharedNetwork6Ptr no_network;

    // Add Subnets 1, 2 and 4 to shared networks.
    ASSERT_NO_THROW(shared_network1->add(subnet1));
    ASSERT_NO_THROW(shared_network2->add(subnet2));
    ASSERT_NO_THROW(shared_network2->add(subnet4));

    // Create our "existing" configured subnets.
    CfgSubnets6 cfg_to;
    ASSERT_NO_THROW(cfg_to.add(subnet1));
    ASSERT_NO_THROW(cfg_to.add(subnet2));
    ASSERT_NO_THROW(cfg_to.add(subnet3));
    ASSERT_NO_THROW(cfg_to.add(subnet4));

    // Merge in an "empty" config. Should have the original config,
    // still intact.
    CfgSubnets6 cfg_from;
    ASSERT_NO_THROW(cfg_to.merge(cfg_def, networks, cfg_from));

    // We should have all four subnets, with no changes.
    ASSERT_EQ(4, cfg_to.getAll()->size());

    // Should be no changes to the configuration.
    ASSERT_NO_FATAL_FAILURE(checkMergedSubnet(cfg_to, "2001:1::/64",
                                              SubnetID(1), 100, shared_network1));
    ASSERT_NO_FATAL_FAILURE(checkMergedSubnet(cfg_to, "2001:2::/64",
                                              SubnetID(2), 100, shared_network2));
    ASSERT_NO_FATAL_FAILURE(checkMergedSubnet(cfg_to, "2001:3::/64",
                                              SubnetID(3), 100, no_network));
    ASSERT_NO_FATAL_FAILURE(checkMergedSubnet(cfg_to, "2001:4::/64",
                                              SubnetID(4), 100, shared_network2));

    // Fill cfg_from configuration with subnets.
    // subnet 1b updates subnet 1 but leaves it in network 1
    Subnet6Ptr subnet1b(new Subnet6(IOAddress("2001:1::"),
                                   64, 2, 3, 100, 400, SubnetID(1)));
    subnet1b->setSharedNetworkName("shared-network1");

    // Add generic option 1 to subnet 1b.
    std::string value("Yay!");
    OptionPtr option(new Option(Option::V6, 1));
    option->setData(value.begin(), value.end());
    ASSERT_NO_THROW(subnet1b->getCfgOption()->add(option, false, "isc"));

    // subnet 3b updates subnet 3 and removes it from network 2
    Subnet6Ptr subnet3b(new Subnet6(IOAddress("2001:3::"),
                                   64, 3, 4, 100, 500, SubnetID(3)));

    // Now Add generic option 1 to subnet 3b.
    value = "Team!";
    option.reset(new Option(Option::V6, 1));
    option->setData(value.begin(), value.end());
    ASSERT_NO_THROW(subnet3b->getCfgOption()->add(option, false, "isc"));

    // subnet 4b updates subnet 4 and moves it from network2 to network 1
    Subnet6Ptr subnet4b(new Subnet6(IOAddress("2001:4::"),
                                   64, 3, 4, 100, 500, SubnetID(4)));
    subnet4b->setSharedNetworkName("shared-network1");

    // subnet 5 is new and belongs to network 2
    // Has two pools both with an option 1
    Subnet6Ptr subnet5(new Subnet6(IOAddress("2001:5::"),
                                   64, 1, 2, 100, 300, SubnetID(5)));
    subnet5->setSharedNetworkName("shared-network2");

    // Add pool 1
    Pool6Ptr pool(new Pool6(Lease::TYPE_NA, IOAddress("2001:5::10"), IOAddress("2001:5::20")));
    value = "POOLS";
    option.reset(new Option(Option::V6, 1));
    option->setData(value.begin(), value.end());
    ASSERT_NO_THROW(pool->getCfgOption()->add(option, false, "isc"));
    subnet5->addPool(pool);

    // Add pool 2
    pool.reset(new Pool6(Lease::TYPE_PD, IOAddress("2001:6::"), 64));
    value ="RULE!";
    option.reset(new Option(Option::V6, 1));
    option->setData(value.begin(), value.end());
    ASSERT_NO_THROW(pool->getCfgOption()->add(option, false, "isc"));
    subnet5->addPool(pool);

    // Add subnets to the merge from config.
    ASSERT_NO_THROW(cfg_from.add(subnet1b));
    ASSERT_NO_THROW(cfg_from.add(subnet3b));
    ASSERT_NO_THROW(cfg_from.add(subnet4b));
    ASSERT_NO_THROW(cfg_from.add(subnet5));

    // Merge again.
    ASSERT_NO_THROW(cfg_to.merge(cfg_def, networks, cfg_from));
    ASSERT_EQ(5, cfg_to.getAll()->size());

    // The subnet1 should be replaced by subnet1b.
    ASSERT_NO_FATAL_FAILURE(checkMergedSubnet(cfg_to, "2001:1::/64",
                                              SubnetID(1), 400, shared_network1));

    // Let's verify that our option is there and populated correctly.
    auto subnet = cfg_to.getByPrefix("2001:1::/64");
    auto desc = subnet->getCfgOption()->get("isc", 1);
    ASSERT_TRUE(desc.option_);
    OptionStringPtr opstr = boost::dynamic_pointer_cast<OptionString>(desc.option_);
    ASSERT_TRUE(opstr);
    EXPECT_EQ("Yay!", opstr->getValue());

    // The subnet2 should not be affected because it was not present.
    ASSERT_NO_FATAL_FAILURE(checkMergedSubnet(cfg_to, "2001:2::/64",
                                              SubnetID(2), 100, shared_network2));

    // subnet3 should be replaced by subnet3b and no longer assigned to a network.
    ASSERT_NO_FATAL_FAILURE(checkMergedSubnet(cfg_to, "2001:3::/64",
                                              SubnetID(3), 500, no_network));
    // Let's verify that our option is there and populated correctly.
    subnet = cfg_to.getByPrefix("2001:3::/64");
    desc = subnet->getCfgOption()->get("isc", 1);
    ASSERT_TRUE(desc.option_);
    opstr = boost::dynamic_pointer_cast<OptionString>(desc.option_);
    ASSERT_TRUE(opstr);
    EXPECT_EQ("Team!", opstr->getValue());

    // subnet4 should be replaced by subnet4b and moved to network1.
    ASSERT_NO_FATAL_FAILURE(checkMergedSubnet(cfg_to, "2001:4::/64",
                                              SubnetID(4), 500, shared_network1));

    // subnet5 should have been added to configuration.
    ASSERT_NO_FATAL_FAILURE(checkMergedSubnet(cfg_to, "2001:5::/64",
                                              SubnetID(5), 300, shared_network2));

    // Let's verify that both pools have the proper options.
    subnet = cfg_to.getByPrefix("2001:5::/64");
    const PoolPtr merged_pool = subnet->getPool(Lease::TYPE_NA, IOAddress("2001:5::10"));
    ASSERT_TRUE(merged_pool);
    desc = merged_pool->getCfgOption()->get("isc", 1);
    opstr = boost::dynamic_pointer_cast<OptionString>(desc.option_);
    ASSERT_TRUE(opstr);
    EXPECT_EQ("POOLS", opstr->getValue());

    const PoolPtr merged_pool2 = subnet->getPool(Lease::TYPE_PD, IOAddress("2001:1::"));
    ASSERT_TRUE(merged_pool2);
    desc = merged_pool2->getCfgOption()->get("isc", 1);
    opstr = boost::dynamic_pointer_cast<OptionString>(desc.option_);
    ASSERT_TRUE(opstr);
    EXPECT_EQ("RULE!", opstr->getValue());
}

} // end of anonymous namespace
