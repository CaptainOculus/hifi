//
//  EntityServer.cpp
//  assignment-client/src/entities
//
//  Created by Brad Hefta-Gaub on 4/29/14
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <QTimer>
#include <EntityTree.h>
#include <SimpleEntitySimulation.h>

#include "EntityServer.h"
#include "EntityServerConsts.h"
#include "EntityNodeData.h"

const char* MODEL_SERVER_NAME = "Entity";
const char* MODEL_SERVER_LOGGING_TARGET_NAME = "entity-server";
const char* LOCAL_MODELS_PERSIST_FILE = "resources/models.svo";

EntityServer::EntityServer(NLPacket& packet) :
    OctreeServer(packet),
    _entitySimulation(NULL)
{
    auto& packetReceiver = DependencyManager::get<NodeList>()->getPacketReceiver();
    packetReceiver.registerListenerForTypes({ PacketType::EntityAdd, PacketType::EntityEdit, PacketType::EntityErase },
                                            this, "handleEntityPacket");
}

EntityServer::~EntityServer() {
    if (_pruneDeletedEntitiesTimer) {
        _pruneDeletedEntitiesTimer->stop();
        _pruneDeletedEntitiesTimer->deleteLater();
    }

    EntityTreePointer tree = std::static_pointer_cast<EntityTree>(_tree);
    tree->removeNewlyCreatedHook(this);
}

void EntityServer::handleEntityPacket(QSharedPointer<NLPacket> packet, SharedNodePointer senderNode) {
    if (_octreeInboundPacketProcessor) {
        _octreeInboundPacketProcessor->queueReceivedPacket(packet, senderNode);
    }
}

OctreeQueryNode* EntityServer::createOctreeQueryNode() {
    return new EntityNodeData();
}

OctreePointer EntityServer::createTree() {
    EntityTreePointer tree = EntityTreePointer(new EntityTree(true));
    tree->createRootElement();
    tree->addNewlyCreatedHook(this);
    if (!_entitySimulation) {
        SimpleEntitySimulation* simpleSimulation = new SimpleEntitySimulation();
        simpleSimulation->setEntityTree(tree);
        tree->setSimulation(simpleSimulation);
        _entitySimulation = simpleSimulation;
    }
    return tree;
}

void EntityServer::beforeRun() {
    _pruneDeletedEntitiesTimer = new QTimer();
    connect(_pruneDeletedEntitiesTimer, SIGNAL(timeout()), this, SLOT(pruneDeletedEntities()));
    const int PRUNE_DELETED_MODELS_INTERVAL_MSECS = 1 * 1000; // once every second
    _pruneDeletedEntitiesTimer->start(PRUNE_DELETED_MODELS_INTERVAL_MSECS);
}

void EntityServer::entityCreated(const EntityItem& newEntity, const SharedNodePointer& senderNode) {
}


// EntityServer will use the "special packets" to send list of recently deleted entities
bool EntityServer::hasSpecialPacketsToSend(const SharedNodePointer& node) {
    bool shouldSendDeletedEntities = false;

    // check to see if any new entities have been added since we last sent to this node...
    EntityNodeData* nodeData = static_cast<EntityNodeData*>(node->getLinkedData());
    if (nodeData) {
        quint64 deletedEntitiesSentAt = nodeData->getLastDeletedEntitiesSentAt();
        EntityTreePointer tree = std::static_pointer_cast<EntityTree>(_tree);
        shouldSendDeletedEntities = tree->hasEntitiesDeletedSince(deletedEntitiesSentAt);

        #ifdef EXTRA_ERASE_DEBUGGING
            if (shouldSendDeletedEntities) {
                int elapsed = usecTimestampNow() - deletedEntitiesSentAt;
                qDebug() << "shouldSendDeletedEntities to node:" << node->getUUID() << "deletedEntitiesSentAt:" << deletedEntitiesSentAt << "elapsed:" << elapsed;
            }
        #endif
    }

    return shouldSendDeletedEntities;
}

// FIXME - most of the old code for this was encapsulated in EntityTree, I liked that design from a data
// hiding and object oriented perspective. But that didn't really allow us to handle the case of lots
// of entities being deleted at the same time. I'd like to look to move this back into EntityTree but
// for now this works and addresses the bug.
int EntityServer::sendSpecialPackets(const SharedNodePointer& node, OctreeQueryNode* queryNode, int& packetsSent) {
    int totalBytes = 0;

    EntityNodeData* nodeData = static_cast<EntityNodeData*>(node->getLinkedData());
    if (nodeData) {

        quint64 deletedEntitiesSentAt = nodeData->getLastDeletedEntitiesSentAt();
        quint64 considerEntitiesSince = EntityTree::getAdjustedConsiderSince(deletedEntitiesSentAt);

        quint64 deletePacketSentAt = usecTimestampNow();
        EntityTreePointer tree = std::static_pointer_cast<EntityTree>(_tree);
        auto recentlyDeleted = tree->getRecentlyDeletedEntityIDs();

        packetsSent = 0;

        // create a new special packet
        std::unique_ptr<NLPacket> deletesPacket = NLPacket::create(PacketType::EntityErase);

        // pack in flags
        OCTREE_PACKET_FLAGS flags = 0;
        deletesPacket->writePrimitive(flags);

        // pack in sequence number
        auto sequenceNumber = queryNode->getSequenceNumber();
        deletesPacket->writePrimitive(sequenceNumber);

        // pack in timestamp
        OCTREE_PACKET_SENT_TIME now = usecTimestampNow();
        deletesPacket->writePrimitive(now);

        // figure out where we are now and pack a temporary number of IDs
        uint16_t numberOfIDs = 0;
        qint64 numberOfIDsPos = deletesPacket->pos();
        deletesPacket->writePrimitive(numberOfIDs);

        // we keep a multi map of entity IDs to timestamps, we only want to include the entity IDs that have been
        // deleted since we last sent to this node
        auto it = recentlyDeleted.constBegin();
        while (it != recentlyDeleted.constEnd()) {

            // if the timestamp is more recent then out last sent time, include it
            if (it.key() > considerEntitiesSince) {

                // get all the IDs for this timestamp
                const auto& entityIDsFromTime = recentlyDeleted.values(it.key());

                for (const auto& entityID : entityIDsFromTime) {

                    // check to make sure we have room for one more ID, if we don't have more
                    // room, then send out this packet and create another one
                    if (NUM_BYTES_RFC4122_UUID > deletesPacket->bytesAvailableForWrite()) {

                        // replace the count for the number of included IDs
                        deletesPacket->seek(numberOfIDsPos);
                        deletesPacket->writePrimitive(numberOfIDs);

                        // Send the current packet
                        queryNode->packetSent(*deletesPacket);
                        auto thisPacketSize = deletesPacket->getDataSize();
                        totalBytes += thisPacketSize;
                        packetsSent++;
                        DependencyManager::get<NodeList>()->sendPacket(std::move(deletesPacket), *node);

                        #ifdef EXTRA_ERASE_DEBUGGING
                            qDebug() << "EntityServer::sendSpecialPackets() sending packet packetsSent[" << packetsSent << "] size:" << thisPacketSize;
                        #endif


                        // create another packet
                        deletesPacket = NLPacket::create(PacketType::EntityErase);

                        // pack in flags
                        deletesPacket->writePrimitive(flags);

                        // pack in sequence number
                        sequenceNumber = queryNode->getSequenceNumber();
                        deletesPacket->writePrimitive(sequenceNumber);

                        // pack in timestamp
                        deletesPacket->writePrimitive(now);

                        // figure out where we are now and pack a temporary number of IDs
                        numberOfIDs = 0;
                        numberOfIDsPos = deletesPacket->pos();
                        deletesPacket->writePrimitive(numberOfIDs);
                    }

                    // FIXME - we still seem to see cases where incorrect EntityIDs get sent from the server
                    // to the client. These were causing "lost" entities like flashlights and laser pointers
                    // now that we keep around some additional history of the erased entities and resend that
                    // history for a longer time window, these entities are not "lost". But we haven't yet
                    // found/fixed the underlying issue that caused bad UUIDs to be sent to some users.
                    deletesPacket->write(entityID.toRfc4122());
                    ++numberOfIDs;

                    #ifdef EXTRA_ERASE_DEBUGGING
                        qDebug() << "EntityTree::encodeEntitiesDeletedSince() including:" << entityID;
                    #endif
                } // end for (ids)

            } // end if (it.val > sinceLast)


            ++it;
        } // end while

        // replace the count for the number of included IDs
        deletesPacket->seek(numberOfIDsPos);
        deletesPacket->writePrimitive(numberOfIDs);

        // Send the current packet
        queryNode->packetSent(*deletesPacket);
        auto thisPacketSize = deletesPacket->getDataSize();
        totalBytes += thisPacketSize;
        packetsSent++;
        DependencyManager::get<NodeList>()->sendPacket(std::move(deletesPacket), *node);
        #ifdef EXTRA_ERASE_DEBUGGING
            qDebug() << "EntityServer::sendSpecialPackets() sending packet packetsSent[" << packetsSent << "] size:" << thisPacketSize;
        #endif

        nodeData->setLastDeletedEntitiesSentAt(deletePacketSentAt);
    }

    #ifdef EXTRA_ERASE_DEBUGGING
        if (packetsSent > 0) {
            qDebug() << "EntityServer::sendSpecialPackets() sent " << packetsSent << "special packets of " 
                        << totalBytes << " total bytes to node:" << node->getUUID();
        }
    #endif

    // TODO: caller is expecting a packetLength, what if we send more than one packet??
    return totalBytes;
}


void EntityServer::pruneDeletedEntities() {
    EntityTreePointer tree = std::static_pointer_cast<EntityTree>(_tree);
    if (tree->hasAnyDeletedEntities()) {

        quint64 earliestLastDeletedEntitiesSent = usecTimestampNow() + 1; // in the future
        DependencyManager::get<NodeList>()->eachNode([&earliestLastDeletedEntitiesSent](const SharedNodePointer& node) {
            if (node->getLinkedData()) {
                EntityNodeData* nodeData = static_cast<EntityNodeData*>(node->getLinkedData());
                quint64 nodeLastDeletedEntitiesSentAt = nodeData->getLastDeletedEntitiesSentAt();
                if (nodeLastDeletedEntitiesSentAt < earliestLastDeletedEntitiesSent) {
                    earliestLastDeletedEntitiesSent = nodeLastDeletedEntitiesSentAt;
                }
            }
        });
        tree->forgetEntitiesDeletedBefore(earliestLastDeletedEntitiesSent);
    }
}

bool EntityServer::readAdditionalConfiguration(const QJsonObject& settingsSectionObject) {
    bool wantEditLogging = false;
    readOptionBool(QString("wantEditLogging"), settingsSectionObject, wantEditLogging);
    qDebug("wantEditLogging=%s", debug::valueOf(wantEditLogging));

    bool wantTerseEditLogging = false;
    readOptionBool(QString("wantTerseEditLogging"), settingsSectionObject, wantTerseEditLogging);
    qDebug("wantTerseEditLogging=%s", debug::valueOf(wantTerseEditLogging));

    EntityTreePointer tree = std::static_pointer_cast<EntityTree>(_tree);
    tree->setWantEditLogging(wantEditLogging);
    tree->setWantTerseEditLogging(wantTerseEditLogging);

    return true;
}
